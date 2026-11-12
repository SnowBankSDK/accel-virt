#include "device/display/display_show.hpp"

#include "core/trace/function_scope.hpp"
#include "device/abi/entry_points.hpp"

namespace av {

DisplayShow::DisplayShow(Host &host, HostRoutine exporter, void *context)
    : host_(host), exporter_(exporter), context_(context), lock_(nullptr), wake_(nullptr), exited_(nullptr),
      thread_(nullptr), stopping_(false), abandoned_(false), generation_(0u), held_{}, images_{}, asked_{}, keys_{}
{
    AV_SCOPE("DisplayShow::DisplayShow");
}

DisplayShow::~DisplayShow()
{
    AV_SCOPE("DisplayShow::~DisplayShow");
    stop();
    if (abandoned_) return;
    if (wake_ != nullptr) host_.destroy_event(wake_);
    if (exited_ != nullptr) host_.destroy_event(exited_);
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
}

Status DisplayShow::start()
{
    AV_SCOPE("DisplayShow::start");
    host_.require_main_loop("DisplayShow::start");
    if (thread_ != nullptr) return Status::ok;
    if (abandoned_) return Status::invalid_state;
    Status status = lock_ != nullptr ? Status::ok : host_.create_mutex(lock_);
    if (status == Status::ok && wake_ == nullptr) status = host_.create_event(wake_);
    if (status == Status::ok && exited_ == nullptr) status = host_.create_event(exited_);
    if (status != Status::ok) return status;
    stopping_ = false;
    return host_.start_thread(&AvDisplayShowRun, this, thread_);
}

void DisplayShow::wake()
{
    AV_SCOPE("DisplayShow::wake");
    if (wake_ != nullptr) host_.signal_event(wake_);
}

void DisplayShow::ask(uint32_t head, uint64_t key)
{
    AV_SCOPE("DisplayShow::ask");
    if (head >= DisplayClock::heads || lock_ == nullptr) return;
    host_.lock_mutex(lock_);
    asked_[head] = true;
    keys_[head] = key;
    host_.unlock_mutex(lock_);
}

bool DisplayShow::take_asked(uint32_t head, uint64_t &key)
{
    AV_SCOPE("DisplayShow::take_asked");
    if (head >= DisplayClock::heads || lock_ == nullptr) return false;
    host_.lock_mutex(lock_);
    const bool asked = asked_[head];
    if (asked) key = keys_[head];
    asked_[head] = false;
    host_.unlock_mutex(lock_);
    return asked;
}

void DisplayShow::forget()
{
    AV_SCOPE("DisplayShow::forget");
    if (lock_ == nullptr) return;
    host_.lock_mutex(lock_);
    for (bool &held : held_) held = false;
    for (bool &asked : asked_) asked = false;
    __atomic_store_n(&generation_, generation_ + 1u, __ATOMIC_RELEASE);
    host_.unlock_mutex(lock_);
}

uint32_t DisplayShow::generation() const
{
    AV_SCOPE("DisplayShow::generation");
    return __atomic_load_n(&generation_, __ATOMIC_ACQUIRE);
}

void DisplayShow::stop()
{
    AV_SCOPE("DisplayShow::stop");
    if (thread_ == nullptr) return;
    host_.lock_mutex(lock_);
    stopping_ = true;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    if (!host_.wait_event_until(exited_, host_.monotonic_nanoseconds() + stop_budget_nanoseconds)) {
        host_.log("virtio-gpu-av: the display export thread did not leave the host driver within 2000 ms; "
                  "it is left running");
        abandoned_ = true;
        thread_ = nullptr;
        return;
    }
    host_.join_thread(thread_);
    thread_ = nullptr;
    forget();
}

void DisplayShow::run()
{
    AV_SCOPE("DisplayShow::run");
    for (;;) {
        host_.wait_event(wake_);
        host_.lock_mutex(lock_);
        const bool stopping = stopping_;
        host_.unlock_mutex(lock_);
        if (stopping) break;
        exporter_(context_);
    }
    host_.signal_event(exited_);
}

void DisplayShow::hold(uint32_t head, const AvExportedImage &image, uint32_t generation)
{
    AV_SCOPE("DisplayShow::hold");
    if (head >= DisplayClock::heads || lock_ == nullptr) return;
    host_.lock_mutex(lock_);
    if (generation == generation_) {
        images_[head] = image;
        held_[head] = true;
    }
    host_.unlock_mutex(lock_);
}

bool DisplayShow::take(uint32_t head, AvExportedImage &image)
{
    AV_SCOPE("DisplayShow::take");
    if (head >= DisplayClock::heads || lock_ == nullptr) return false;
    host_.lock_mutex(lock_);
    const bool taken = held_[head];
    if (taken) image = images_[head];
    held_[head] = false;
    host_.unlock_mutex(lock_);
    return taken;
}

bool DisplayShow::running() const
{
    AV_SCOPE("DisplayShow::running");
    return thread_ != nullptr;
}

bool DisplayShow::invariants_hold() const
{
    AV_SCOPE("DisplayShow::invariants_hold");
    return thread_ == nullptr || (lock_ != nullptr && wake_ != nullptr && exited_ != nullptr);
}

}
