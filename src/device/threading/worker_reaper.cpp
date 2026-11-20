#include "device/threading/worker_reaper.hpp"

#include "core/trace/function_scope.hpp"
#include "device/abi/entry_points.hpp"
#include "device/threading/worker.hpp"

namespace av {

WorkerReaper::WorkerReaper(Host &host, HostRoutine reaped, void *owner)
    : host_(host), reaped_(reaped), owner_(owner), thread_(nullptr), wake_(nullptr), drained_(nullptr), lock_(nullptr),
      entries_{}, held_(0u), stopping_(false), scheduled_(false), quiet_(false), abandoned_(false)
{
    AV_SCOPE("WorkerReaper::WorkerReaper");
}

WorkerReaper::~WorkerReaper()
{
    AV_SCOPE("WorkerReaper::~WorkerReaper");
    if (abandoned_) return;
    stop();
    release();
}

Status WorkerReaper::start()
{
    AV_SCOPE("WorkerReaper::start");
    host_.require_main_loop("WorkerReaper::start");
    if (thread_ != nullptr || abandoned_) return Status::invalid_state;
    stopping_ = false;
    quiet_ = false;
    Status status = wake_ == nullptr ? host_.create_event(wake_) : Status::ok;
    if (status == Status::ok && drained_ == nullptr) status = host_.create_event(drained_);
    if (status == Status::ok && lock_ == nullptr) status = host_.create_mutex(lock_);
    if (status == Status::ok) status = host_.start_thread(&AvWorkerReaperRun, this, thread_);
    if (status != Status::ok && held_ == 0u) release();
    return status;
}

Status WorkerReaper::take(Worker &worker, uint32_t token)
{
    AV_SCOPE("WorkerReaper::take");
    host_.require_main_loop("WorkerReaper::take");
    if (thread_ == nullptr) return Status::invalid_state;
    host_.lock_mutex(lock_);
    uint32_t at = capacity;
    for (uint32_t index = 0u; index < capacity && at == capacity; index++)
        if (entries_[index].state_ == State::free) at = index;
    if (at != capacity) {
        entries_[at] = Entry{&worker, token, State::waiting, false, false, false};
        held_++;
    }
    host_.unlock_mutex(lock_);
    if (at == capacity) return Status::no_space;
    worker.watch_exit(wake_);
    host_.signal_event(wake_);
    return Status::ok;
}

bool WorkerReaper::tell_when_parted(const Worker &worker)
{
    AV_SCOPE("WorkerReaper::tell_when_parted");
    host_.require_main_loop("WorkerReaper::tell_when_parted");
    if (thread_ == nullptr || worker.parted()) return false;
    bool watched = false;
    host_.lock_mutex(lock_);
    for (Entry &entry : entries_) {
        if (entry.state_ != State::waiting || entry.worker_ != &worker) continue;
        entry.watched_ = true;
        watched = true;
    }
    host_.unlock_mutex(lock_);
    if (watched) host_.signal_event(wake_);
    return watched;
}

uint32_t WorkerReaper::collect(uint32_t *tokens, uint32_t room)
{
    AV_SCOPE("WorkerReaper::collect");
    host_.require_main_loop("WorkerReaper::collect");
    if (lock_ == nullptr) return 0u;
    uint32_t taken = 0u;
    host_.lock_mutex(lock_);
    scheduled_ = false;
    for (uint32_t index = 0u; index < capacity && taken < room; index++) {
        if (entries_[index].state_ != State::joined) continue;
        tokens[taken++] = entries_[index].token_;
        entries_[index] = Entry{};
        held_--;
    }
    host_.unlock_mutex(lock_);
    return taken;
}

uint32_t WorkerReaper::drain(uint64_t deadline, uint32_t &newly_overdue)
{
    AV_SCOPE("WorkerReaper::drain");
    host_.require_main_loop("WorkerReaper::drain");
    newly_overdue = 0u;
    if (thread_ == nullptr) return 0u;
    host_.lock_mutex(lock_);
    quiet_ = true;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    for (;;) {
        host_.lock_mutex(lock_);
        const uint32_t left = waiting();
        host_.unlock_mutex(lock_);
        if (left == 0u) return 0u;
        if (!host_.wait_event_until(drained_, deadline)) break;
    }
    host_.lock_mutex(lock_);
    for (Entry &entry : entries_) {
        if (entry.state_ != State::waiting || entry.overdue_) continue;
        entry.overdue_ = true;
        newly_overdue++;
    }
    const uint32_t left = waiting();
    host_.unlock_mutex(lock_);
    return left;
}

void WorkerReaper::resume()
{
    AV_SCOPE("WorkerReaper::resume");
    host_.require_main_loop("WorkerReaper::resume");
    if (thread_ == nullptr) return;
    host_.lock_mutex(lock_);
    quiet_ = false;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
}

void WorkerReaper::abandon()
{
    AV_SCOPE("WorkerReaper::abandon");
    host_.require_main_loop("WorkerReaper::abandon");
    if (thread_ == nullptr) return;
    host_.lock_mutex(lock_);
    quiet_ = true;
    stopping_ = true;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    abandoned_ = true;
}

uint32_t WorkerReaper::waiting() const
{
    AV_SCOPE("WorkerReaper::waiting");
    uint32_t count = 0u;
    for (const Entry &entry : entries_) count += entry.state_ == State::waiting ? 1u : 0u;
    return count;
}

void WorkerReaper::stop()
{
    AV_SCOPE("WorkerReaper::stop");
    host_.require_main_loop("WorkerReaper::stop");
    if (thread_ == nullptr || abandoned_) return;
    host_.lock_mutex(lock_);
    stopping_ = true;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    host_.join_thread(thread_);
    thread_ = nullptr;
    stopping_ = false;
}

bool WorkerReaper::running() const
{
    AV_SCOPE("WorkerReaper::running");
    return thread_ != nullptr;
}

uint32_t WorkerReaper::held()
{
    AV_SCOPE("WorkerReaper::held");
    if (lock_ == nullptr) return 0u;
    host_.lock_mutex(lock_);
    const uint32_t count = held_;
    host_.unlock_mutex(lock_);
    return count;
}

uint32_t WorkerReaper::fresh()
{
    AV_SCOPE("WorkerReaper::fresh");
    if (lock_ == nullptr) return 0u;
    uint32_t count = 0u;
    host_.lock_mutex(lock_);
    for (const Entry &entry : entries_) count += entry.state_ == State::waiting && !entry.overdue_ ? 1u : 0u;
    host_.unlock_mutex(lock_);
    return count;
}

Worker *WorkerReaper::exited_at(uint32_t index)
{
    AV_SCOPE("WorkerReaper::exited_at");
    host_.lock_mutex(lock_);
    const Entry &entry = entries_[index];
    Worker *const exited = entry.state_ == State::waiting && entry.worker_->exited() ? entry.worker_ : nullptr;
    host_.unlock_mutex(lock_);
    return exited;
}

bool WorkerReaper::note_parted()
{
    AV_SCOPE("WorkerReaper::note_parted");
    bool parted = false;
    for (Entry &entry : entries_) {
        if (entry.state_ != State::waiting || !entry.watched_ || entry.parted_ || !entry.worker_->parted()) continue;
        entry.parted_ = true;
        parted = true;
    }
    return parted;
}

void WorkerReaper::join_exited()
{
    AV_SCOPE("WorkerReaper::join_exited");
    for (uint32_t index = 0u; index < capacity; index++) {
        Worker *const worker = exited_at(index);
        if (worker == nullptr) continue;
        worker->join();
        host_.lock_mutex(lock_);
        entries_[index].state_ = State::joined;
        if (!scheduled_ && !stopping_ && !quiet_) {
            scheduled_ = true;
            host_.run_on_main_loop(reaped_, owner_);
        }
        host_.unlock_mutex(lock_);
    }
}

void WorkerReaper::run()
{
    AV_SCOPE("WorkerReaper::run");
    host_.require_worker("WorkerReaper::run");
    for (;;) {
        host_.wait_event(wake_);
        join_exited();
        host_.lock_mutex(lock_);
        const bool settled = waiting() == 0u;
        const bool over = stopping_ && settled;
        bool       owed = false;
        const bool parted = !quiet_ && !stopping_ && note_parted();
        if (!quiet_ && !stopping_ && !scheduled_) {
            owed = parted;
            for (const Entry &entry : entries_) owed = owed || entry.state_ == State::joined;
            scheduled_ = owed;
        }
        host_.unlock_mutex(lock_);
        if (owed) host_.run_on_main_loop(reaped_, owner_);
        if (settled) host_.signal_event(drained_);
        if (over) return;
    }
}

void WorkerReaper::release()
{
    AV_SCOPE("WorkerReaper::release");
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
    if (wake_ != nullptr) host_.destroy_event(wake_);
    if (drained_ != nullptr) host_.destroy_event(drained_);
    lock_ = nullptr;
    wake_ = nullptr;
    drained_ = nullptr;
    thread_ = nullptr;
}

bool WorkerReaper::invariants_hold() const
{
    AV_SCOPE("WorkerReaper::invariants_hold");
    uint32_t counted = 0u;
    for (const Entry &entry : entries_) {
        if (entry.state_ == State::free) continue;
        if (entry.worker_ == nullptr) return false;
        counted++;
    }
    return counted == held_;
}

}
