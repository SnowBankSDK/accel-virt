#include "um/d3d/kernel_lane.hpp"

#include "core/log/keyed_log.hpp"
#include "core/platform/block_source.hpp"
#include "core/platform/thread_lock_hold.hpp"
#include "core/trace/function_scope.hpp"
#include "proto/context/sub_api.hpp"
#include "um/d3d/d3d_session.hpp"

namespace av::d3d {

KernelLane::KernelLane(const D3DDDI_DEVICECALLBACKS &kernel, HANDLE device, HANDLE context, D3dSession &session,
                       KeyedLog &lines)
    : kernel_(kernel), device_(device), context_(context), session_(session), lines_(lines), transport_(),
      stream_(), staging_(nullptr), batch_(nullptr), written_(0u), tried_(false)
{
    AV_SCOPE("KernelLane::KernelLane");
}

KernelLane::~KernelLane()
{
    AV_SCOPE("KernelLane::~KernelLane");
    if (!opened()) {
        close();
        return;
    }
    uint64_t newest = 0u;
    {
        const ThreadLockHold held(session_.lock());
        static_cast<void>(stream_.object().flush());
        newest = transport_.object().published_fence();
    }
    if (!transport_.object().faulted() && !transport_.object().host_faulted())
        static_cast<void>(transport_.object().await_gpu(newest));
    const ThreadLockHold held(session_.lock());
    close();
}

bool KernelLane::open()
{
    AV_SCOPE("KernelLane::open");
    const ThreadLockHold held(session_.lock());
    if (tried_) return opened();
    tried_ = true;
    if (context_ == nullptr) return false;
    BlockSource &memory = session_.memory();
    staging_ = static_cast<uint8_t *>(memory.allocate(staging_bytes, Pool::paged));
    batch_ = static_cast<uint8_t *>(memory.allocate(batch_bytes, Pool::paged));
    if (staging_ == nullptr || batch_ == nullptr) {
        close();
        lines_.once("a context's work stays on its session's ring: no memory for its own lane");
        return false;
    }
    static_cast<void>(transport_.construct(session_.connection(), memory, SubApi::d3d, ring_order, reply_bytes,
                                           staging_, staging_bytes));
    if (transport_.object().initialize_lane(session_.transport(), *this) != Status::ok) {
        close();
        lines_.once("a context's work stays on its session's ring: the kernel gave it no lane of its own");
        return false;
    }
    transport_.object().hold_in(batch_, batch_bytes);
    static_cast<void>(stream_.construct(transport_.object()));
    stream_.object().speak_with(session_.select_command(), session_.flush_command());
    return true;
}

void KernelLane::close()
{
    AV_SCOPE("KernelLane::close");
    stream_.destroy();
    transport_.destroy();
    BlockSource &memory = session_.memory();
    if (staging_ != nullptr) memory.release(staging_);
    if (batch_ != nullptr) memory.release(batch_);
    staging_ = nullptr;
    batch_ = nullptr;
}

bool KernelLane::opened() const
{
    AV_SCOPE("KernelLane::opened");
    return stream_.constructed();
}

D3dCommandStream &KernelLane::stream()
{
    AV_SCOPE("KernelLane::stream");
    return stream_.object();
}

bool KernelLane::ran(uint64_t fence) const
{
    AV_SCOPE("KernelLane::ran");
    return !opened() || transport_.object().faulted() || transport_.object().gpu_completed() >= fence;
}

uint64_t KernelLane::newest() const
{
    AV_SCOPE("KernelLane::newest");
    return opened() ? transport_.object().published_fence() : 0u;
}

void KernelLane::writing(D3DKMT_HANDLE allocation)
{
    AV_SCOPE("KernelLane::writing");
    written_ = allocation;
}

Status KernelLane::release(const uint8_t *record, uint32_t size)
{
    AV_SCOPE("KernelLane::release");
    D3DDDICB_SUBMITCOMMAND submit = {};
    submit.Commands = transport_.object().marker_address();
    submit.CommandLength = sizeof(uint64_t);
    submit.BroadcastContextCount = 1u;
    submit.BroadcastContext[0] = context_;
    if (written_ != 0u) {
        submit.NumPrimaries = 1u;
        submit.WrittenPrimaries[0] = written_;
    }
    submit.pPrivateDriverData = const_cast<uint8_t *>(record);
    submit.PrivateDriverDataSize = size;
    if (SUCCEEDED(kernel_.pfnSubmitCommandCb(device_, &submit))) return Status::ok;
    lines_.once("the kernel refused the packet that releases a lane's batch");
    return Status::invalid_state;
}

void KernelLane::waited_long()
{
    AV_SCOPE("KernelLane::waited_long");
    lines_.once("a call waited several seconds for a lane's batch: it may be held behind a wait on its context that "
                "only the application's own thread satisfies later");
}

}
