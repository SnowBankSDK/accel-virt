#include "device/threading/worker_reaper.hpp"

#include "core/trace/function_scope.hpp"
#include "device/abi/entry_points.hpp"
#include "device/threading/worker.hpp"

namespace av {

WorkerReaper::WorkerReaper(Host &host, HostRoutine reaped, void *owner)
    : host_(host), reaped_(reaped), owner_(owner), thread_(nullptr), wake_(nullptr), lock_(nullptr), entries_{},
      held_(0u), stopping_(false), scheduled_(false)
{
    AV_SCOPE("WorkerReaper::WorkerReaper");
}

WorkerReaper::~WorkerReaper()
{
    AV_SCOPE("WorkerReaper::~WorkerReaper");
    stop();
    release();
}

Status WorkerReaper::start()
{
    AV_SCOPE("WorkerReaper::start");
    host_.require_main_loop("WorkerReaper::start");
    if (thread_ != nullptr) return Status::invalid_state;
    stopping_ = false;
    Status status = wake_ == nullptr ? host_.create_event(wake_) : Status::ok;
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
        entries_[at] = Entry{&worker, token, State::waiting};
        held_++;
    }
    host_.unlock_mutex(lock_);
    if (at == capacity) return Status::no_space;
    worker.watch_exit(wake_);
    host_.signal_event(wake_);
    return Status::ok;
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

void WorkerReaper::stop()
{
    AV_SCOPE("WorkerReaper::stop");
    host_.require_main_loop("WorkerReaper::stop");
    if (thread_ == nullptr) return;
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

Worker *WorkerReaper::exited_at(uint32_t index)
{
    AV_SCOPE("WorkerReaper::exited_at");
    host_.lock_mutex(lock_);
    const Entry &entry = entries_[index];
    Worker *const exited = entry.state_ == State::waiting && entry.worker_->exited() ? entry.worker_ : nullptr;
    host_.unlock_mutex(lock_);
    return exited;
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
        if (!scheduled_ && !stopping_) {
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
        bool waiting = false;
        for (const Entry &entry : entries_) waiting = waiting || entry.state_ == State::waiting;
        const bool over = stopping_ && !waiting;
        host_.unlock_mutex(lock_);
        if (over) return;
    }
}

void WorkerReaper::release()
{
    AV_SCOPE("WorkerReaper::release");
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
    if (wake_ != nullptr) host_.destroy_event(wake_);
    lock_ = nullptr;
    wake_ = nullptr;
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
