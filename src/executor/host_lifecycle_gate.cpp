#include "executor/host_lifecycle_gate.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/egl_entry_gate.hpp"

#include <time.h>

namespace av {

HostLifecycleGate::HostLifecycleGate(uint64_t wait_ns)
    : lock_(), wait_ns_(wait_ns), entries_(0u), shares_(0u), refusals_(0u), shares_compiles_(false), egl_(nullptr)
{
    AV_SCOPE("HostLifecycleGate::HostLifecycleGate");
    pthread_rwlockattr_t attributes;
    pthread_rwlockattr_init(&attributes);
    pthread_rwlockattr_setkind_np(&attributes, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    pthread_rwlock_init(&lock_, &attributes);
    pthread_rwlockattr_destroy(&attributes);
}

HostLifecycleGate::~HostLifecycleGate()
{
    AV_SCOPE("HostLifecycleGate::~HostLifecycleGate");
    pthread_rwlock_destroy(&lock_);
}

void HostLifecycleGate::driven_by(VkDriverId driver)
{
    AV_SCOPE("HostLifecycleGate::driven_by");
    shares_compiles_ = driver == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
    if (excludes_egl()) egl_->need();
}

void HostLifecycleGate::guard_egl(EglEntryGate &egl)
{
    AV_SCOPE("HostLifecycleGate::guard_egl");
    egl_ = &egl;
    if (excludes_egl()) egl_->need();
}

bool HostLifecycleGate::excludes_egl() const
{
    AV_SCOPE("HostLifecycleGate::excludes_egl");
    return egl_ != nullptr && shares_compiles_;
}

bool HostLifecycleGate::shares_compiles() const
{
    AV_SCOPE("HostLifecycleGate::shares_compiles");
    return shares_compiles_;
}

timespec HostLifecycleGate::deadline() const
{
    AV_SCOPE("HostLifecycleGate::deadline");
    constexpr uint64_t second_ns = 1000000000ull;
    timespec           until = {};
    clock_gettime(CLOCK_MONOTONIC, &until);
    const uint64_t at = static_cast<uint64_t>(until.tv_nsec) + wait_ns_ % second_ns;
    until.tv_sec += static_cast<time_t>(wait_ns_ / second_ns + at / second_ns);
    until.tv_nsec = static_cast<long>(at % second_ns);
    return until;
}

bool HostLifecycleGate::enter()
{
    AV_SCOPE("HostLifecycleGate::enter");
    const timespec until = deadline();
    if (pthread_rwlock_clockwrlock(&lock_, CLOCK_MONOTONIC, &until) != 0) {
        __atomic_add_fetch(&refusals_, 1u, __ATOMIC_RELAXED);
        return false;
    }
    if (excludes_egl() && !egl_->exclude(wait_ns_)) {
        pthread_rwlock_unlock(&lock_);
        __atomic_add_fetch(&refusals_, 1u, __ATOMIC_RELAXED);
        return false;
    }
    __atomic_add_fetch(&entries_, 1u, __ATOMIC_RELAXED);
    return true;
}

void HostLifecycleGate::leave()
{
    AV_SCOPE("HostLifecycleGate::leave");
    if (excludes_egl()) egl_->admit();
    pthread_rwlock_unlock(&lock_);
}

bool HostLifecycleGate::share()
{
    AV_SCOPE("HostLifecycleGate::share");
    if (!shares_compiles_) return true;
    const timespec until = deadline();
    if (pthread_rwlock_clockrdlock(&lock_, CLOCK_MONOTONIC, &until) != 0) {
        __atomic_add_fetch(&refusals_, 1u, __ATOMIC_RELAXED);
        return false;
    }
    __atomic_add_fetch(&shares_, 1u, __ATOMIC_RELAXED);
    return true;
}

void HostLifecycleGate::unshare()
{
    AV_SCOPE("HostLifecycleGate::unshare");
    if (shares_compiles_) pthread_rwlock_unlock(&lock_);
}

uint64_t HostLifecycleGate::entries() const
{
    AV_SCOPE("HostLifecycleGate::entries");
    return __atomic_load_n(&entries_, __ATOMIC_RELAXED);
}

uint64_t HostLifecycleGate::shares() const
{
    AV_SCOPE("HostLifecycleGate::shares");
    return __atomic_load_n(&shares_, __ATOMIC_RELAXED);
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

HostLifecycleShare::HostLifecycleShare(HostLifecycleGate *gate)
    : gate_(gate), held_(gate == nullptr || gate->share())
{
    AV_SCOPE("HostLifecycleShare::HostLifecycleShare");
}

HostLifecycleShare::~HostLifecycleShare()
{
    AV_SCOPE("HostLifecycleShare::~HostLifecycleShare");
    if (gate_ != nullptr && held_) gate_->unshare();
}

bool HostLifecycleShare::held() const
{
    AV_SCOPE("HostLifecycleShare::held");
    return held_;
}

}
