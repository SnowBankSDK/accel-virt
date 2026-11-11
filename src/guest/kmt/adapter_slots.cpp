#include "guest/kmt/adapter_slots.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

AdapterSlots::AdapterSlots() : luids_{}, holds_{}
{
    AV_SCOPE("AdapterSlots::AdapterSlots");
}

uint32_t AdapterSlots::claim(uint64_t luid, bool &fresh)
{
    AV_SCOPE("AdapterSlots::claim");
    fresh = false;
    if (luid == 0u) return none;
    for (uint32_t slot = 0u; slot < capacity; slot++)
        if (luids_[slot] == luid) return slot;
    uint32_t idle = none;
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        if (luids_[slot] == 0u) {
            idle = slot;
            break;
        }
        if (idle == none && holds_[slot] == 0u) idle = slot;
    }
    if (idle == none) return none;
    luids_[idle] = luid;
    holds_[idle] = 0u;
    fresh = true;
    return idle;
}

void AdapterSlots::hold(uint32_t slot)
{
    AV_SCOPE("AdapterSlots::hold");
    if (slot < capacity && luids_[slot] != 0u) holds_[slot]++;
}

bool AdapterSlots::release(uint32_t slot)
{
    AV_SCOPE("AdapterSlots::release");
    if (slot >= capacity || holds_[slot] == 0u) return false;
    holds_[slot]--;
    if (holds_[slot] != 0u) return false;
    luids_[slot] = 0u;
    return true;
}

void AdapterSlots::vacate(uint32_t slot)
{
    AV_SCOPE("AdapterSlots::vacate");
    if (slot >= capacity) return;
    luids_[slot] = 0u;
    holds_[slot] = 0u;
}

uint32_t AdapterSlots::holds(uint32_t slot) const
{
    AV_SCOPE("AdapterSlots::holds");
    return slot < capacity ? holds_[slot] : 0u;
}

uint64_t AdapterSlots::luid(uint32_t slot) const
{
    AV_SCOPE("AdapterSlots::luid");
    return slot < capacity ? luids_[slot] : 0u;
}

uint32_t AdapterSlots::used() const
{
    AV_SCOPE("AdapterSlots::used");
    uint32_t used = 0u;
    for (uint32_t slot = 0u; slot < capacity; slot++)
        if (luids_[slot] != 0u) used++;
    return used;
}

bool AdapterSlots::invariants_hold() const
{
    AV_SCOPE("AdapterSlots::invariants_hold");
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        if (luids_[slot] == 0u && holds_[slot] != 0u) return false;
        for (uint32_t other = slot + 1u; other < capacity; other++)
            if (luids_[slot] != 0u && luids_[slot] == luids_[other]) return false;
    }
    return true;
}

}
