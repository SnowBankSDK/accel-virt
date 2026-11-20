#include "device/present/present_engine.hpp"

#include "core/trace/function_scope.hpp"
#include "device/abi/entry_points.hpp"
#include "device/control/control_request.hpp"
#include "device/control/held_requests.hpp"
#include "device/fence/fence_interrupts.hpp"
#include "device/host/host.hpp"
#include "proto/pages/present_progress.hpp"
#include "proto/virtio_gpu/gpu_response.hpp"
#include "proto/virtio_gpu/queued_present_request.hpp"
#include "proto/wire/wire_reader.hpp"

namespace av {

PresentEngine::PresentEngine(Host &host, FenceInterrupts &interrupts, HeldRequests &held)
    : host_(host), interrupts_(interrupts), requests_(held), lock_(nullptr), wake_(nullptr), exited_(nullptr),
      thread_(nullptr), stopping_(false), parked_(true), busy_(false), owed_(false), abandoned_(false), generation_(0u),
      waiting_ticket_(HeldRequests::no_ticket), waiting_address_(0u), waiting_snapshots_(), page_(host), snapshots_(), slots_(), first_(0u), moved_(0u), held_(0u), counted_(0u), completed_(0u), refused_(0u)
{
    AV_SCOPE("PresentEngine::PresentEngine");
    for (ObjectStorage<QueuedPresent> &storage : slots_) static_cast<void>(storage.construct(host));
}

PresentEngine::~PresentEngine()
{
    AV_SCOPE("PresentEngine::~PresentEngine");
    stop_thread();
    if (abandoned_) return;
    for (ObjectStorage<QueuedPresent> &storage : slots_) storage.destroy();
    if (wake_ != nullptr) host_.destroy_event(wake_);
    if (exited_ != nullptr) host_.destroy_event(exited_);
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
}

Status PresentEngine::initialize()
{
    AV_SCOPE("PresentEngine::initialize");
    host_.require_main_loop("PresentEngine::initialize");
    if (lock_ != nullptr) return Status::invalid_state;
    Status status = host_.create_mutex(lock_);
    if (status == Status::ok) status = host_.create_event(wake_);
    if (status == Status::ok) status = host_.create_event(exited_);
    if (status == Status::ok) return Status::ok;
    if (wake_ != nullptr) host_.destroy_event(wake_);
    wake_ = nullptr;
    if (lock_ != nullptr) host_.destroy_mutex(lock_);
    lock_ = nullptr;
    return status;
}

QueuedPresent &PresentEngine::slot(uint32_t index)
{
    AV_SCOPE("PresentEngine::slot");
    return slots_[index % depth].object();
}

Status PresentEngine::attach(uint64_t address, const PresentFrames &snapshots)
{
    AV_SCOPE("PresentEngine::attach");
    host_.require_main_loop("PresentEngine::attach");
    if (lock_ == nullptr || wake_ == nullptr || exited_ == nullptr) return Status::invalid_state;
    detach();
    if (releasing()) return Status::conflict;
    if (address == 0u) return Status::ok;
    snapshots_ = snapshots;
    const Status mapped = page_.map(address);
    if (mapped != Status::ok) return mapped;
    if (thread_ == nullptr && (abandoned_ || host_.start_thread(&AvPresentEngineRun, this, thread_) != Status::ok)) {
        thread_ = nullptr;
        page_.release();
        return Status::no_memory;
    }
    host_.lock_mutex(lock_);
    parked_ = false;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    return Status::ok;
}

bool PresentEngine::releasing()
{
    AV_SCOPE("PresentEngine::releasing");
    if (lock_ == nullptr) return false;
    host_.lock_mutex(lock_);
    const bool owed = owed_;
    host_.unlock_mutex(lock_);
    return owed;
}

void PresentEngine::attach_when_released(uint32_t ticket, uint64_t address, const PresentFrames &snapshots)
{
    AV_SCOPE("PresentEngine::attach_when_released");
    host_.require_main_loop("PresentEngine::attach_when_released");
    if (waiting_ticket_ != HeldRequests::no_ticket) requests_.conclude(waiting_ticket_, Status::conflict);
    waiting_ticket_ = ticket;
    waiting_address_ = address;
    waiting_snapshots_ = snapshots;
    if (!releasing()) released();
}

void PresentEngine::detach()
{
    AV_SCOPE("PresentEngine::detach");
    host_.require_main_loop("PresentEngine::detach");
    if (lock_ == nullptr) return;
    if (waiting_ticket_ != HeldRequests::no_ticket) requests_.conclude(waiting_ticket_, Status::conflict);
    waiting_ticket_ = HeldRequests::no_ticket;
    host_.lock_mutex(lock_);
    parked_ = true;
    generation_++;
    const bool busy = busy_;
    owed_ = owed_ || busy;
    host_.unlock_mutex(lock_);
    if (!busy) release_slots();
}

void PresentEngine::released()
{
    AV_SCOPE("PresentEngine::released");
    host_.require_main_loop("PresentEngine::released");
    if (releasing()) return;
    release_slots();
    const uint32_t ticket = waiting_ticket_;
    waiting_ticket_ = HeldRequests::no_ticket;
    if (ticket != HeldRequests::no_ticket) requests_.conclude(ticket, attach(waiting_address_, waiting_snapshots_));
}

void PresentEngine::release_slots()
{
    AV_SCOPE("PresentEngine::release_slots");
    for (uint32_t index = 0u; index < depth; index++) slot(index).release();
    first_ = 0u;
    moved_ = 0u;
    held_ = 0u;
    counted_ = 0u;
    completed_ = 0u;
    refused_ = 0u;
    page_.release();
}

void PresentEngine::stop_thread()
{
    AV_SCOPE("PresentEngine::stop_thread");
    if (thread_ == nullptr) return;
    host_.lock_mutex(lock_);
    stopping_ = true;
    host_.unlock_mutex(lock_);
    host_.signal_event(wake_);
    if (!host_.wait_event_until(exited_, host_.monotonic_nanoseconds() + stop_budget_nanoseconds)) {
        host_.log("virtio-gpu-av: the present engine's thread did not leave the host driver within 2000 ms; it is "
                  "left running");
        abandoned_ = true;
        thread_ = nullptr;
        return;
    }
    host_.join_thread(thread_);
    thread_ = nullptr;
}

uint32_t PresentEngine::queue(ControlRequest &request)
{
    AV_SCOPE("PresentEngine::queue");
    host_.require_main_loop("PresentEngine::queue");
    if (lock_ == nullptr || !page_.mapped()) return GpuResponse::err_invalid_parameter;
    host_.lock_mutex(lock_);
    release_moved();
    const uint64_t ticket = ++counted_;
    host_.unlock_mutex(lock_);
    return take(request, ticket);
}

void PresentEngine::release_moved()
{
    AV_SCOPE("PresentEngine::release_moved");
    for (; moved_ != 0u; moved_--, held_--, first_ = (first_ + 1u) % depth) slot(first_).release();
}

uint32_t PresentEngine::take(ControlRequest &request, uint64_t ticket)
{
    AV_SCOPE("PresentEngine::take");
    uint8_t              fixed[QueuedPresentRequest::encoded_size] = {};
    WireReader           reader(fixed, request.read(0u, fixed, sizeof fixed));
    QueuedPresentRequest present;
    Status               status = present.decode(reader);
    if (status == Status::ok && (present.ticket() != ticket || held_ == depth)) status = Status::invalid_parameter;
    if (status == Status::ok) status = slot(first_ + held_).accept(request, present, snapshots_);
    host_.lock_mutex(lock_);
    const bool completed_now = status != Status::ok && refuse();
    if (status == Status::ok) held_++;
    host_.unlock_mutex(lock_);
    if (status == Status::ok) host_.signal_event(wake_);
    if (completed_now) interrupts_.raise(FenceInterrupts::present_line);
    if (status == Status::ok) return GpuResponse::ok_nodata;
    return status == Status::no_memory ? GpuResponse::err_out_of_memory : GpuResponse::err_invalid_parameter;
}

bool PresentEngine::refuse()
{
    AV_SCOPE("PresentEngine::refuse");
    if (moved_ != held_) {
        slot(first_ + held_ - 1u).follow_with_refusal();
        return false;
    }
    completed_ = counted_;
    refused_++;
    page_.publish(PresentProgress(completed_, refused_));
    return true;
}

void PresentEngine::run()
{
    AV_SCOPE("PresentEngine::run");
    uint32_t generation = 0u;
    for (QueuedPresent *present = next(generation); present != nullptr; present = next(generation))
        complete(*present, present->move(snapshots_), generation);
    host_.signal_event(exited_);
}

QueuedPresent *PresentEngine::next(uint32_t &generation)
{
    AV_SCOPE("PresentEngine::next");
    host_.lock_mutex(lock_);
    while (!stopping_ && (parked_ || moved_ == held_)) {
        host_.unlock_mutex(lock_);
        host_.wait_event(wake_);
        host_.lock_mutex(lock_);
    }
    QueuedPresent *const present = stopping_ ? nullptr : &slot(first_ + moved_);
    busy_ = present != nullptr;
    generation = generation_;
    host_.unlock_mutex(lock_);
    return present;
}

void PresentEngine::complete(QueuedPresent &present, Status moved, uint32_t generation)
{
    AV_SCOPE("PresentEngine::complete");
    host_.lock_mutex(lock_);
    busy_ = false;
    if (generation != generation_) {
        const bool owed = owed_;
        owed_ = false;
        host_.unlock_mutex(lock_);
        if (owed) host_.run_on_main_loop(&AvPresentEngineReleased, this);
        return;
    }
    moved_++;
    completed_ = present.ticket() + present.refusals_after();
    refused_ += present.refusals_after() + (moved == Status::ok ? 0u : 1u);
    page_.publish(PresentProgress(completed_, refused_));
    host_.unlock_mutex(lock_);
    interrupts_.raise(FenceInterrupts::present_line);
}

bool PresentEngine::invariants_hold() const
{
    AV_SCOPE("PresentEngine::invariants_hold");
    if (first_ >= depth || moved_ > held_ || held_ > depth) return false;
    if (refused_ > completed_ || completed_ > counted_) return false;
    for (const ObjectStorage<QueuedPresent> &storage : slots_)
        if (!storage.constructed() || !storage.object().invariants_hold()) return false;
    return thread_ == nullptr || (lock_ != nullptr && wake_ != nullptr && exited_ != nullptr);
}

}
