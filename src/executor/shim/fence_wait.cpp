#include "executor/shim/fence_wait.hpp"

#include "core/platform/thread_lock_hold.hpp"
#include "core/support/scope_exit.hpp"
#include "core/trace/function_scope.hpp"
#include "executor/shim/d3d_headers.hpp"

#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace av {

namespace {

constexpr uint64_t nanoseconds_per_ms = 1000000u;

}

FenceWait::FenceWait() : event_(), waiting_(), clock_()
{
    AV_SCOPE("FenceWait::FenceWait");
}

void FenceWait::drain()
{
    AV_SCOPE("FenceWait::drain");
    uint64_t counted = 0u;
    while (read(event_.fd(), &counted, sizeof counted) > 0) {
    }
}

bool FenceWait::wait(ID3D11Fence *fence, uint64_t value, uint32_t wait_ms)
{
    AV_SCOPE("FenceWait::wait");
    if (fence == nullptr) return false;
    ScopeExit            let_go([fence] { fence->Release(); });
    const ThreadLockHold serial(waiting_);
    if (fence->GetCompletedValue() >= value) return true;
    if (!event_.held()) event_.hold(eventfd(0u, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!event_.held()) return false;
    drain();
    if (FAILED(fence->SetEventOnCompletion(value, reinterpret_cast<void *>(static_cast<intptr_t>(event_.fd())))))
        return false;
    const uint64_t deadline = clock_.now_nanoseconds() + uint64_t{wait_ms} * nanoseconds_per_ms;
    for (;;) {
        const uint64_t now = clock_.now_nanoseconds();
        if (now >= deadline) return fence->GetCompletedValue() >= value;
        pollfd waiting = {event_.fd(), POLLIN, 0};
        const auto     left_ms = static_cast<int>((deadline - now + nanoseconds_per_ms - 1u) / nanoseconds_per_ms);
        const int      ready = poll(&waiting, 1, left_ms);
        if (ready < 0 && errno != EINTR) return false;
        if (ready > 0) drain();
        if (fence->GetCompletedValue() >= value) return true;
    }
}

}
