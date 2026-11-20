#include "device/execution/session_gate.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_signal.hpp"
#include "device/host/host.hpp"

namespace av {

extern "C" void av_session_gate_raised(void *gate)
{
    static_cast<SessionGate *>(gate)->raise();
}

SessionGate::SessionGate(Host &host)
    : host_(host), run_(nullptr), listeners_lock_(nullptr), runner_(nullptr), consumed_(0u),
      waiting_(0u), closed_(false), listeners_{}, listening_(0u), waiters_{}
{
    AV_SCOPE("SessionGate::SessionGate");
}

SessionGate::~SessionGate()
{
    AV_SCOPE("SessionGate::~SessionGate");
    if (run_ != nullptr) host_.destroy_mutex(run_);
    if (listeners_lock_ != nullptr) host_.destroy_mutex(listeners_lock_);
}

Status SessionGate::initialize()
{
    AV_SCOPE("SessionGate::initialize");
    if (ready()) return Status::ok;
    if (run_ == nullptr && host_.create_mutex(run_) != Status::ok) return Status::no_memory;
    if (listeners_lock_ == nullptr && host_.create_mutex(listeners_lock_) != Status::ok) return Status::no_memory;
    __atomic_store_n(&closed_, false, __ATOMIC_RELEASE);
    return Status::ok;
}

bool SessionGate::ready() const
{
    AV_SCOPE("SessionGate::ready");
    return run_ != nullptr && listeners_lock_ != nullptr;
}

void SessionGate::close()
{
    AV_SCOPE("SessionGate::close");
    if (!ready()) return;
    __atomic_store_n(&closed_, true, __ATOMIC_SEQ_CST);
    wake_waiters();
}

void SessionGate::withdraw()
{
    AV_SCOPE("SessionGate::withdraw");
    host_.require_main_loop("SessionGate::withdraw");
    if (!ready()) return;
    __atomic_store_n(&closed_, true, __ATOMIC_SEQ_CST);
    signal_waiters();
}

bool SessionGate::closed() const
{
    AV_SCOPE("SessionGate::closed");
    return __atomic_load_n(&closed_, __ATOMIC_SEQ_CST);
}

bool SessionGate::enter(const void *runner)
{
    AV_SCOPE("SessionGate::enter");
    host_.lock_mutex(run_);
    if (runner == nullptr) return false;
    const bool switched = runner_ != nullptr && runner_ != runner;
    runner_ = runner;
    return switched;
}

void SessionGate::leave()
{
    AV_SCOPE("SessionGate::leave");
    host_.unlock_mutex(run_);
}

void SessionGate::consumed(uint64_t head)
{
    AV_SCOPE("SessionGate::consumed");
    __atomic_store_n(&consumed_, head, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&waiting_, __ATOMIC_SEQ_CST) != 0u) wake_waiters();
}

uint64_t SessionGate::consumed() const
{
    AV_SCOPE("SessionGate::consumed");
    return __atomic_load_n(&consumed_, __ATOMIC_SEQ_CST);
}

bool SessionGate::reached(uint64_t end, uint64_t deadline, void *wake)
{
    AV_SCOPE("SessionGate::reached");
    if (consumed() >= end) return true;
    if (closed() || wake == nullptr) return false;
    __atomic_fetch_add(&waiting_, 1u, __ATOMIC_SEQ_CST);
    if (consumed() < end && !closed()) static_cast<void>(host_.wait_event_until(wake, deadline));
    __atomic_fetch_sub(&waiting_, 1u, __ATOMIC_SEQ_CST);
    return consumed() >= end;
}

Status SessionGate::attend(void *wake)
{
    AV_SCOPE("SessionGate::attend");
    if (!ready() || wake == nullptr) return Status::invalid_state;
    host_.lock_mutex(listeners_lock_);
    uint32_t slot = 0u;
    while (slot < ContextCapacity::lanes && waiters_[slot] != nullptr) slot++;
    if (slot < ContextCapacity::lanes) __atomic_store_n(&waiters_[slot], wake, __ATOMIC_SEQ_CST);
    host_.unlock_mutex(listeners_lock_);
    return slot < ContextCapacity::lanes ? Status::ok : Status::no_space;
}

void SessionGate::depart(const void *wake)
{
    AV_SCOPE("SessionGate::depart");
    if (!ready()) return;
    host_.lock_mutex(listeners_lock_);
    for (void *&waiter : waiters_) {
        if (waiter != wake) continue;
        __atomic_store_n(&waiter, static_cast<void *>(nullptr), __ATOMIC_SEQ_CST);
        break;
    }
    host_.unlock_mutex(listeners_lock_);
}

void SessionGate::wake_waiters()
{
    AV_SCOPE("SessionGate::wake_waiters");
    host_.lock_mutex(listeners_lock_);
    signal_waiters();
    host_.unlock_mutex(listeners_lock_);
}

void SessionGate::signal_waiters()
{
    AV_SCOPE("SessionGate::signal_waiters");
    for (void *&waiter : waiters_) {
        void *const wake = __atomic_load_n(&waiter, __ATOMIC_SEQ_CST);
        if (wake != nullptr) host_.signal_event(wake);
    }
}

Status SessionGate::listen(ContextSignal &signal)
{
    AV_SCOPE("SessionGate::listen");
    if (!ready()) return Status::invalid_state;
    host_.lock_mutex(listeners_lock_);
    const bool room = listening_ < most_listeners;
    if (room) listeners_[listening_++] = &signal;
    host_.unlock_mutex(listeners_lock_);
    return room ? Status::ok : Status::no_space;
}

void SessionGate::forget(const ContextSignal &signal)
{
    AV_SCOPE("SessionGate::forget");
    if (!ready()) return;
    host_.lock_mutex(listeners_lock_);
    for (uint32_t index = 0u; index < listening_; index++) {
        if (listeners_[index] != &signal) continue;
        listeners_[index] = listeners_[--listening_];
        listeners_[listening_] = nullptr;
        break;
    }
    host_.unlock_mutex(listeners_lock_);
}

void SessionGate::raise()
{
    AV_SCOPE("SessionGate::raise");
    host_.lock_mutex(listeners_lock_);
    for (uint32_t index = 0u; index < listening_; index++) listeners_[index]->raise();
    host_.unlock_mutex(listeners_lock_);
}

bool SessionGate::invariants_hold() const
{
    AV_SCOPE("SessionGate::invariants_hold");
    for (uint32_t index = 0u; index < most_listeners; index++)
        if ((index < listening_) != (listeners_[index] != nullptr)) return false;
    return listening_ <= most_listeners;
}

}
