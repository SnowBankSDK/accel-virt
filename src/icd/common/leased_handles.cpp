#include "icd/common/leased_handles.hpp"

#include "core/trace/function_scope.hpp"

namespace av::icd {

LeasedHandles::LeasedHandles(BlockSource &memory) : lease_(), held_(memory, Pool::paged)
{
    AV_SCOPE("LeasedHandles::LeasedHandles");
}

bool LeasedHandles::take(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes)
{
    AV_SCOPE("LeasedHandles::take");
    return lease_.take(transport, kind, bytes);
}

void LeasedHandles::keep(uint64_t handle, LeaseRecord::Kind kind, uint64_t bytes)
{
    AV_SCOPE("LeasedHandles::keep");
    if (bytes == 0u) return;
    uint32_t slot = 0u;
    static_cast<void>(held_.insert(handle, Held{bytes, static_cast<uint32_t>(kind)}, slot));
}

void LeasedHandles::refund(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes)
{
    AV_SCOPE("LeasedHandles::refund");
    lease_.give(transport, kind, bytes);
}

void LeasedHandles::release(ClientTransport &transport, uint64_t handle)
{
    AV_SCOPE("LeasedHandles::release");
    Held held = {};
    if (held_.take(handle, held)) lease_.give(transport, static_cast<LeaseRecord::Kind>(held.kind_), held.bytes_);
}

void LeasedHandles::reset()
{
    AV_SCOPE("LeasedHandles::reset");
    held_.clear();
    lease_.reset();
}

uint64_t LeasedHandles::left(LeaseRecord::Kind kind) const
{
    AV_SCOPE("LeasedHandles::left");
    return lease_.left(kind);
}

uint32_t LeasedHandles::count() const
{
    AV_SCOPE("LeasedHandles::count");
    return held_.count();
}

}
