#include "device/control/held_requests.hpp"

#include "core/trace/function_scope.hpp"
#include "device/control/control_request.hpp"
#include "device/host/host.hpp"
#include "proto/virtio_gpu/gpu_response.hpp"

namespace av {

static_assert(HeldRequests::capacity - 1u <= (1u << 8u) - 1u, "a ticket carries its slot in its low byte");

HeldRequests::HeldRequests(Host &host) : host_(host), held_{}, live_(0u), serial_(0u)
{
    AV_SCOPE("HeldRequests::HeldRequests");
}

HeldRequests::~HeldRequests()
{
    AV_SCOPE("HeldRequests::~HeldRequests");
    drop_all();
}

Status HeldRequests::hold(ControlRequest &request, Answering answering, uint32_t &ticket)
{
    AV_SCOPE("HeldRequests::hold");
    host_.require_main_loop("HeldRequests::hold");
    ticket = no_ticket;
    if (answering == nullptr) return Status::invalid_parameter;
    uint32_t free = capacity;
    for (uint32_t index = 0u; index < capacity && free == capacity; index++)
        if (held_[index].kept_ == nullptr) free = index;
    if (free == capacity) return Status::no_space;
    void *const kept = request.keep();
    if (kept == nullptr) return Status::no_memory;
    serial_ = serial_ == (UINT32_MAX >> index_bits) ? 1u : serial_ + 1u;
    held_[free] = Held{kept, answering, serial_};
    live_++;
    ticket = serial_ << index_bits | free;
    return Status::ok;
}

const HeldRequests::Held *HeldRequests::slot_of(uint32_t ticket) const
{
    AV_SCOPE("HeldRequests::slot_of");
    const uint32_t index = ticket & index_mask;
    if (ticket == no_ticket || index >= capacity) return nullptr;
    const Held &held = held_[index];
    return held.kept_ != nullptr && held.serial_ == ticket >> index_bits ? &held : nullptr;
}

bool HeldRequests::holds(uint32_t ticket) const
{
    AV_SCOPE("HeldRequests::holds");
    return slot_of(ticket) != nullptr;
}

void *HeldRequests::kept(uint32_t ticket) const
{
    AV_SCOPE("HeldRequests::kept");
    const Held *const held = slot_of(ticket);
    return held != nullptr ? held->kept_ : nullptr;
}

void HeldRequests::conclude(uint32_t ticket, Status status)
{
    AV_SCOPE("HeldRequests::conclude");
    const Held *const held = slot_of(ticket);
    if (held == nullptr) return;
    GpuResponse response;
    response.set(held->answering_(status));
    answer(ticket, response);
}

void HeldRequests::answer(uint32_t ticket, const GpuResponse &response)
{
    AV_SCOPE("HeldRequests::answer");
    host_.require_main_loop("HeldRequests::answer");
    const Held *const held = slot_of(ticket);
    if (held == nullptr) return;
    ControlRequest kept(host_, held->kept_);
    kept.respond(response);
    release(ticket & index_mask, true);
}

void HeldRequests::release(uint32_t index, bool answered)
{
    AV_SCOPE("HeldRequests::release");
    void *const kept = held_[index].kept_;
    held_[index] = Held{};
    live_--;
    host_.release_request(kept, answered);
}

void HeldRequests::drop_all()
{
    AV_SCOPE("HeldRequests::drop_all");
    for (uint32_t index = 0u; index < capacity && live_ != 0u; index++)
        if (held_[index].kept_ != nullptr) release(index, false);
}

uint32_t HeldRequests::held() const
{
    AV_SCOPE("HeldRequests::held");
    return live_;
}

bool HeldRequests::invariants_hold() const
{
    AV_SCOPE("HeldRequests::invariants_hold");
    uint32_t counted = 0u;
    for (const Held &held : held_) {
        if (held.kept_ == nullptr) continue;
        if (held.answering_ == nullptr || held.serial_ == 0u) return false;
        counted++;
    }
    return counted == live_;
}

}
