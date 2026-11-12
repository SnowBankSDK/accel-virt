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
    : host_(host), run_(nullptr), listeners_lock_(nullptr), progress_(nullptr), runner_(nullptr), consumed_(0u),
      waiting_(0u), closed_(false), listeners_{}, listening_(0u)
{
    AV_SCOPE("SessionGate::SessionGate");
}

SessionGate::~SessionGate()
{
    AV_SCOPE("SessionGate::~SessionGate");
    if (run_ != nullptr) host_.destroy_mutex(run_);
    if (listeners_lock_ != nullptr) host_.destroy_mutex(listeners_lock_);
    if (progress_ != nullptr) host_.destroy_event(progress_);
}

Status SessionGate::initialize()
{
    AV_SCOPE("SessionGate::initialize");
    if (ready()) return Status::ok;
    if (run_ == nullptr && host_.create_mutex(run_) != Status::ok) return Status::no_memory;
    if (listeners_lock_ == nullptr && host_.create_mutex(listeners_lock_) != Status::ok) return Status::no_memory;
    if (progress_ == nullptr && host_.create_event(progress_) != Status::ok) return Status::no_memory;
    __atomic_store_n(&closed_, false, __ATOMIC_RELEASE);
    return Status::ok;
}

bool SessionGate::ready() const
{
    AV_SCOPE("SessionGate::ready");
    return run_ != nullptr && listeners_lock_ != nullptr && progress_ != nullptr;
}

void SessionGate::close()
{
    AV_SCOPE("SessionGate::close");
    if (!ready()) return;
    __atomic_store_n(&closed_, true, __ATOMIC_RELEASE);
    host_.signal_event(progress_);
}

bool SessionGate::closed() const
{
    AV_SCOPE("SessionGate::closed");
    return __atomic_load_n(&closed_, __ATOMIC_ACQUIRE);
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
    if (__atomic_load_n(&waiting_, __ATOMIC_SEQ_CST) != 0u) host_.signal_event(progress_);
}

uint64_t SessionGate::consumed() const
{
    AV_SCOPE("SessionGate::consumed");
    return __atomic_load_n(&consumed_, __ATOMIC_SEQ_CST);
}

bool SessionGate::reached(uint64_t end, uint64_t deadline)
{
    AV_SCOPE("SessionGate::reached");
    if (consumed() >= end) return true;
    if (closed()) return false;
    __atomic_fetch_add(&waiting_, 1u, __ATOMIC_SEQ_CST);
    const uint64_t now = host_.monotonic_nanoseconds();
    const uint64_t until = deadline - now > wait_slice_ns ? now + wait_slice_ns : deadline;
    if (consumed() < end && !closed() && now < deadline) static_cast<void>(host_.wait_event_until(progress_, until));
    __atomic_fetch_sub(&waiting_, 1u, __ATOMIC_SEQ_CST);
    return consumed() >= end;
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
