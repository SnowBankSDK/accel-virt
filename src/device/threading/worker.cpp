#include "device/threading/worker.hpp"

#include "core/trace/function_scope.hpp"
#include "core/trace/state_writer.hpp"
#include "device/abi/entry_points.hpp"
#include "device/context/context_signal.hpp"
#include "device/host/host.hpp"
#include "device/context/ring_consumer.hpp"

namespace av {

Worker::Worker(Host &host, RingConsumer &ring)
    : host_(host), ring_(ring), signal_(nullptr), doorbell_(host), thread_(nullptr), wake_(nullptr), lock_(nullptr), queue_{}, head_(0u), count_(0u),
      stopping_(false), exit_watch_(nullptr), exited_(false), joined_(false), left_(nullptr)
      {
          AV_SCOPE("Worker::Worker");
      }

Worker::~Worker()
{
    AV_SCOPE("Worker::~Worker");
    stop();
    join();
    release();
}

Status Worker::start()
{
    AV_SCOPE("Worker::start");
    host_.require_main_loop("Worker::start");
    if (running()) return Status::invalid_state;
    release();
    stopping_ = false;
    exit_watch_ = nullptr;
    exited_ = false;
    joined_ = false;
    Status status = host_.create_event(wake_);
    if (status == Status::ok) status = host_.create_mutex(lock_);
    if (status == Status::ok) status = host_.start_thread(&AvWorkerRun, this, thread_);
    if (status != Status::ok) release();
    return status;
}

Status Worker::post(const WorkItem &item)
{
    AV_SCOPE("Worker::post");
    host_.require_main_loop("Worker::post");
    if (!running()) return Status::invalid_state;
    host_.lock_mutex(lock_);
    const bool stopping = stopping_;
    const bool room = !stopping && count_ < queue_capacity;
    if (room) queue_[(head_ + count_++) % queue_capacity] = item;
    host_.unlock_mutex(lock_);
    if (stopping) return Status::invalid_state;
    if (!room) return Status::no_space;
    host_.signal_event(wake_);
    return Status::ok;
}

Status Worker::connect_doorbell(uint32_t index)
{
    AV_SCOPE("Worker::connect_doorbell");
    host_.require_main_loop("Worker::connect_doorbell");
    if (!running()) return Status::invalid_state;
    return doorbell_.connect(wake_, index);
}

void Worker::connect_signal(ContextSignal &signal)
{
    AV_SCOPE("Worker::connect_signal");
    host_.require_main_loop("Worker::connect_signal");
    signal_ = &signal;
}

void Worker::wake()
{
    AV_SCOPE("Worker::wake");
    if (wake_ == nullptr) return;
    host_.signal_event(wake_);
}

void Worker::disconnect_doorbell()
{
    AV_SCOPE("Worker::disconnect_doorbell");
    host_.require_main_loop("Worker::disconnect_doorbell");
    doorbell_.disconnect();
}

void Worker::stop()
{
    AV_SCOPE("Worker::stop");
    host_.require_main_loop("Worker::stop");
    if (thread_ == nullptr || __atomic_load_n(&joined_, __ATOMIC_ACQUIRE)) return;
    doorbell_.disconnect();
    host_.lock_mutex(lock_);
    const bool asked = stopping_;
    stopping_ = true;
    host_.unlock_mutex(lock_);
    if (asked) return;
    ring_.abandon();
    host_.signal_event(wake_);
}

void Worker::watch_exit(void *event)
{
    AV_SCOPE("Worker::watch_exit");
    host_.require_main_loop("Worker::watch_exit");
    __atomic_store_n(&exit_watch_, event, __ATOMIC_SEQ_CST);
}

bool Worker::exited() const
{
    AV_SCOPE("Worker::exited");
    return __atomic_load_n(&exited_, __ATOMIC_SEQ_CST);
}

void Worker::join()
{
    AV_SCOPE("Worker::join");
    if (thread_ == nullptr || __atomic_load_n(&joined_, __ATOMIC_ACQUIRE)) return;
    host_.join_thread(thread_);
    ring_.thread_joined(__atomic_exchange_n(&left_, static_cast<void *>(nullptr), __ATOMIC_ACQ_REL));
    __atomic_store_n(&joined_, true, __ATOMIC_RELEASE);
}

void Worker::release()
{
    AV_SCOPE("Worker::release");
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
    if (wake_ != nullptr) host_.destroy_event(wake_);
    thread_ = nullptr;
    lock_ = nullptr;
    wake_ = nullptr;
    head_ = 0u;
    count_ = 0u;
    exit_watch_ = nullptr;
}

bool Worker::running() const
{
    AV_SCOPE("Worker::running");
    return thread_ != nullptr && !__atomic_load_n(&joined_, __ATOMIC_ACQUIRE);
}

uint32_t Worker::pending()
{
    AV_SCOPE("Worker::pending");
    host_.require_main_loop("Worker::pending");
    if (!running()) return 0u;
    host_.lock_mutex(lock_);
    const uint32_t queued = count_;
    host_.unlock_mutex(lock_);
    return queued;
}

void Worker::report(StateWriter &writer)
{
    AV_SCOPE("Worker::report");
    writer.open("worker", 0u);
    writer.flag("running", running());
    uint32_t queued = 0u;
    bool     requested = false;
    if (running()) {
        if (!host_.try_lock_mutex(lock_)) {
            writer.flag("busy", true);
            writer.close();
            return;
        }
        queued = count_;
        requested = stopping_;
        host_.unlock_mutex(lock_);
    }
    writer.number("pending", queued);
    writer.flag("stopping", requested);
    writer.close();
}

bool Worker::stopping()
{
    AV_SCOPE("Worker::stopping");
    if (!running()) return false;
    host_.lock_mutex(lock_);
    const bool requested = stopping_;
    host_.unlock_mutex(lock_);
    return requested;
}

Worker::Step Worker::next(WorkItem &item)
{
    AV_SCOPE("Worker::next");
    host_.lock_mutex(lock_);
    Step step = stopping_ ? Step::stop : count_ == 0u ? Step::idle : Step::work;
    if (step == Step::work) {
        item = queue_[head_];
        head_ = (head_ + 1u) % queue_capacity;
        count_--;
    }
    host_.unlock_mutex(lock_);
    return step;
}

void Worker::run()
{
    AV_SCOPE("Worker::run");
    host_.require_worker("Worker::run");
    serve();
    __atomic_store_n(&exited_, true, __ATOMIC_SEQ_CST);
    void *const watch = __atomic_load_n(&exit_watch_, __ATOMIC_SEQ_CST);
    if (watch != nullptr) host_.signal_event(watch);
    __atomic_store_n(&left_, ring_.leave_thread(), __ATOMIC_RELEASE);
}

void Worker::serve()
{
    AV_SCOPE("Worker::serve");
    for (;;) {
        if (!work_within_spin()) {
            ring_.announce_awake(false);
            if (!ring_.has_work()) {
                const uint64_t deadline = ring_.watchdog_deadline();
                if (deadline != 0u) static_cast<void>(host_.wait_event_until(wake_, deadline));
                else host_.wait_event(wake_);
            }
            ring_.announce_awake(true);
        }
        WorkItem item;
        for (Step step = next(item); step != Step::idle; step = next(item)) {
            if (step == Step::stop) {
                forgo_queued();
                return ring_.detach();
            }
            item.run();
            item.complete_on(host_);
        }
        if (signal_ != nullptr) signal_->drain();
        ring_.service();
    }
}

void Worker::forgo_queued()
{
    AV_SCOPE("Worker::forgo_queued");
    host_.lock_mutex(lock_);
    WorkItem forgone[queue_capacity];
    const uint32_t count = count_;
    for (uint32_t index = 0u; index < count; index++) forgone[index] = queue_[(head_ + index) % queue_capacity];
    head_ = 0u;
    count_ = 0u;
    host_.unlock_mutex(lock_);
    for (uint32_t index = 0u; index < count; index++) forgone[index].complete_on(host_);
}

bool Worker::work_within_spin()
{
    AV_SCOPE("Worker::work_within_spin");
    constexpr uint64_t spin_ns = 100000u;
    constexpr uint64_t publishing_ns = 100000000u;
    constexpr uint64_t poll_every_ns = 2000u;
    const uint64_t     started = host_.monotonic_nanoseconds();
    uint64_t           polled = started;
    bool               publishing = ring_.poll();
    for (;;) {
        if (ring_.has_work() || __atomic_load_n(&count_, __ATOMIC_ACQUIRE) != 0u ||
            __atomic_load_n(&stopping_, __ATOMIC_ACQUIRE) || (signal_ != nullptr && signal_->pending()))
            return true;
        const uint64_t now = host_.monotonic_nanoseconds();
        if (publishing && now - polled >= poll_every_ns) {
            publishing = ring_.poll();
            polled = now;
        }
        if (now - started >= (publishing ? publishing_ns : spin_ns)) return false;
        __builtin_ia32_pause();
    }
}

bool Worker::invariants_hold() const
{
    AV_SCOPE("Worker::invariants_hold");
    return count_ <= queue_capacity && head_ < queue_capacity && doorbell_.invariants_hold() &&
           (thread_ != nullptr || !doorbell_.connected());
}

}
