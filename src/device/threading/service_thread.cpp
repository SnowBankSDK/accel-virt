#include "device/threading/service_thread.hpp"

#include "core/trace/function_scope.hpp"
#include "device/abi/entry_points.hpp"
#include "device/host/host.hpp"
#include "device/threading/errand.hpp"

namespace av {

ServiceThread::ServiceThread(Host &host)
    : host_(host), thread_(nullptr), wake_(nullptr), exited_(nullptr), lock_(nullptr), queued_{}, done_{},
      outstanding_(0u), stopping_(false), concluding_(false), abandoned_(false)
{
    AV_SCOPE("ServiceThread::ServiceThread");
}

ServiceThread::~ServiceThread()
{
    AV_SCOPE("ServiceThread::~ServiceThread");
    stop();
    if (!abandoned_) release();
}

Status ServiceThread::start()
{
    AV_SCOPE("ServiceThread::start");
    host_.require_main_loop("ServiceThread::start");
    if (thread_ != nullptr || abandoned_) return Status::invalid_state;
    Status status = wake_ == nullptr ? host_.create_event(wake_) : Status::ok;
    if (status == Status::ok && exited_ == nullptr) status = host_.create_event(exited_);
    if (status == Status::ok && lock_ == nullptr) status = host_.create_mutex(lock_);
    stopping_ = false;
    if (status == Status::ok) status = host_.start_thread(&AvServiceThreadRun, this, thread_);
    if (status != Status::ok) {
        thread_ = nullptr;
        release();
    }
    return status;
}

void ServiceThread::append(Chain &chain, Errand &errand)
{
    AV_SCOPE("ServiceThread::append");
    errand.follow(nullptr);
    if (chain.last_ != nullptr) chain.last_->follow(&errand);
    else chain.first_ = &errand;
    chain.last_ = &errand;
}

Errand *ServiceThread::take(Chain &chain)
{
    AV_SCOPE("ServiceThread::take");
    Errand *const first = chain.first_;
    if (first == nullptr) return nullptr;
    chain.first_ = first->next();
    if (chain.first_ == nullptr) chain.last_ = nullptr;
    first->follow(nullptr);
    return first;
}

Status ServiceThread::post(Errand &errand)
{
    AV_SCOPE("ServiceThread::post");
    host_.require_main_loop("ServiceThread::post");
    if (thread_ == nullptr) return Status::invalid_state;
    host_.lock_mutex(lock_);
    const bool open = !stopping_;
    if (open) {
        append(queued_, errand);
        __atomic_fetch_add(&outstanding_, 1u, __ATOMIC_ACQ_REL);
    }
    host_.unlock_mutex(lock_);
    if (!open) return Status::invalid_state;
    host_.signal_event(wake_);
    return Status::ok;
}

Errand *ServiceThread::next_queued()
{
    AV_SCOPE("ServiceThread::next_queued");
    host_.lock_mutex(lock_);
    while (queued_.first_ == nullptr && !stopping_) {
        host_.unlock_mutex(lock_);
        host_.wait_event(wake_);
        host_.lock_mutex(lock_);
    }
    Errand *const next = take(queued_);
    host_.unlock_mutex(lock_);
    return next;
}

void ServiceThread::finished(Errand &errand)
{
    AV_SCOPE("ServiceThread::finished");
    host_.lock_mutex(lock_);
    append(done_, errand);
    const bool schedule = !concluding_ && !stopping_;
    concluding_ = concluding_ || schedule;
    host_.unlock_mutex(lock_);
    if (schedule) host_.run_on_main_loop(&AvServiceThreadConclude, this);
}

void ServiceThread::run()
{
    AV_SCOPE("ServiceThread::run");
    host_.require_worker("ServiceThread::run");
    for (Errand *errand = next_queued(); errand != nullptr; errand = next_queued()) {
        errand->perform();
        if (!errand->detached()) {
            finished(*errand);
            continue;
        }
        errand->conclude();
        __atomic_fetch_sub(&outstanding_, 1u, __ATOMIC_ACQ_REL);
    }
    host_.signal_event(exited_);
}

void ServiceThread::conclude_done()
{
    AV_SCOPE("ServiceThread::conclude_done");
    host_.require_main_loop("ServiceThread::conclude_done");
    if (lock_ == nullptr) return;
    host_.lock_mutex(lock_);
    Chain done = done_;
    done_ = Chain{};
    concluding_ = false;
    for (Errand *errand = done.first_; errand != nullptr; errand = errand->next())
        __atomic_fetch_sub(&outstanding_, 1u, __ATOMIC_ACQ_REL);
    host_.unlock_mutex(lock_);
    for (Errand *errand = take(done); errand != nullptr; errand = take(done)) errand->conclude();
}

void ServiceThread::stop()
{
    AV_SCOPE("ServiceThread::stop");
    host_.require_main_loop("ServiceThread::stop");
    if (thread_ == nullptr) return;
    host_.lock_mutex(lock_);
    stopping_ = true;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    if (!host_.wait_event_until(exited_, host_.monotonic_nanoseconds() + stop_budget_nanoseconds)) {
        host_.log("virtio-gpu-av: the service thread did not finish its errands within 2000 ms of the teardown; "
                  "it is left running");
        abandoned_ = true;
        thread_ = nullptr;
        return;
    }
    host_.join_thread(thread_);
    thread_ = nullptr;
    conclude_done();
}

bool ServiceThread::running() const
{
    AV_SCOPE("ServiceThread::running");
    return thread_ != nullptr;
}

uint32_t ServiceThread::outstanding() const
{
    AV_SCOPE("ServiceThread::outstanding");
    return __atomic_load_n(&outstanding_, __ATOMIC_ACQUIRE);
}

void ServiceThread::release()
{
    AV_SCOPE("ServiceThread::release");
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
    if (wake_ != nullptr) host_.destroy_event(wake_);
    if (exited_ != nullptr) host_.destroy_event(exited_);
    lock_ = nullptr;
    wake_ = nullptr;
    exited_ = nullptr;
}

bool ServiceThread::invariants_hold() const
{
    AV_SCOPE("ServiceThread::invariants_hold");
    return thread_ == nullptr || (lock_ != nullptr && wake_ != nullptr && exited_ != nullptr);
}

}
