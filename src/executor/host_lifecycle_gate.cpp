#include "executor/host_lifecycle_gate.hpp"

#include "core/trace/function_scope.hpp"

#include <time.h>

namespace av {

HostLifecycleGate::HostLifecycleGate(uint64_t wait_ns) : lock_(), wait_ns_(wait_ns), entries_(0u), refusals_(0u)
{
    AV_SCOPE("HostLifecycleGate::HostLifecycleGate");
    pthread_mutex_init(&lock_, nullptr);
}

HostLifecycleGate::~HostLifecycleGate()
{
    AV_SCOPE("HostLifecycleGate::~HostLifecycleGate");
    pthread_mutex_destroy(&lock_);
}

bool HostLifecycleGate::enter()
{
    AV_SCOPE("HostLifecycleGate::enter");
    constexpr uint64_t second_ns = 1000000000ull;
    timespec           until = {};
    clock_gettime(CLOCK_MONOTONIC, &until);
    const uint64_t at = static_cast<uint64_t>(until.tv_nsec) + wait_ns_ % second_ns;
    until.tv_sec += static_cast<time_t>(wait_ns_ / second_ns + at / second_ns);
    until.tv_nsec = static_cast<long>(at % second_ns);
    if (pthread_mutex_clocklock(&lock_, CLOCK_MONOTONIC, &until) != 0) {
        __atomic_add_fetch(&refusals_, 1u, __ATOMIC_RELAXED);
        return false;
    }
    __atomic_add_fetch(&entries_, 1u, __ATOMIC_RELAXED);
    return true;
}

void HostLifecycleGate::leave()
{
    AV_SCOPE("HostLifecycleGate::leave");
    pthread_mutex_unlock(&lock_);
}

uint64_t HostLifecycleGate::entries() const
{
    AV_SCOPE("HostLifecycleGate::entries");
    return __atomic_load_n(&entries_, __ATOMIC_RELAXED);
}

uint64_t HostLifecycleGate::refusals() const
{
    AV_SCOPE("HostLifecycleGate::refusals");
    return __atomic_load_n(&refusals_, __ATOMIC_RELAXED);
}

HostLifecycleHold::HostLifecycleHold(HostLifecycleGate *gate)
    : gate_(gate), held_(gate == nullptr || gate->enter())
{
    AV_SCOPE("HostLifecycleHold::HostLifecycleHold");
}

HostLifecycleHold::~HostLifecycleHold()
{
    AV_SCOPE("HostLifecycleHold::~HostLifecycleHold");
    if (gate_ != nullptr && held_) gate_->leave();
}

bool HostLifecycleHold::held() const
{
    AV_SCOPE("HostLifecycleHold::held");
    return held_;
}

}
