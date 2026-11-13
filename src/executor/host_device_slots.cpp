#include "executor/host_device_slots.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

namespace {

constexpr uint32_t nvidia_process_slots = 255u;
constexpr uint32_t nvidia_unseen_slots = 16u;
constexpr uint32_t nvidia_device_base = 2u;
constexpr uint32_t nvidia_graphics_queue = 2u;
constexpr uint32_t nvidia_other_queue = 1u;

}

HostDeviceSlots::HostDeviceSlots()
    : capacity_(0u), device_base_(0u), graphics_queue_(0u), other_queue_(0u), held_(0u), refusals_(0u)
{
    AV_SCOPE("HostDeviceSlots::HostDeviceSlots");
}

void HostDeviceSlots::driven_by(VkDriverId driver)
{
    AV_SCOPE("HostDeviceSlots::driven_by");
    const bool pooled = driver == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
    capacity_ = pooled ? nvidia_process_slots - nvidia_unseen_slots : 0u;
    device_base_ = pooled ? nvidia_device_base : 0u;
    graphics_queue_ = pooled ? nvidia_graphics_queue : 0u;
    other_queue_ = pooled ? nvidia_other_queue : 0u;
}

bool HostDeviceSlots::bounded() const
{
    AV_SCOPE("HostDeviceSlots::bounded");
    return capacity_ != 0u;
}

uint32_t HostDeviceSlots::capacity() const
{
    AV_SCOPE("HostDeviceSlots::capacity");
    return capacity_;
}

uint32_t HostDeviceSlots::device_slots(const VkQueueFamilyProperties *families, uint32_t family_count,
                                       const VkDeviceCreateInfo &create) const
{
    AV_SCOPE("HostDeviceSlots::device_slots");
    if (!bounded()) return 0u;
    uint64_t slots = device_base_;
    for (uint32_t at = 0u; at < create.queueCreateInfoCount; at++) {
        const VkDeviceQueueCreateInfo &queues = create.pQueueCreateInfos[at];
        const bool graphics = queues.queueFamilyIndex >= family_count ||
                              (families[queues.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;
        slots += uint64_t{queues.queueCount} * (graphics ? graphics_queue_ : other_queue_);
    }
    return slots > capacity_ ? capacity_ + 1u : static_cast<uint32_t>(slots);
}

bool HostDeviceSlots::admit(VkPhysicalDevice physical, const VkDeviceCreateInfo &create, uint32_t &slots)
{
    AV_SCOPE("HostDeviceSlots::admit");
    slots = 0u;
    if (!bounded()) return true;
    VkQueueFamilyProperties families[most_families];
    uint32_t                count = most_families;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    const uint32_t wanted = device_slots(families, count, create);
    if (!take(wanted)) return false;
    slots = wanted;
    return true;
}

bool HostDeviceSlots::take(uint32_t slots)
{
    AV_SCOPE("HostDeviceSlots::take");
    if (!bounded()) return true;
    uint32_t held = __atomic_load_n(&held_, __ATOMIC_RELAXED);
    for (;;) {
        if (slots > capacity_ - held) {
            __atomic_add_fetch(&refusals_, 1u, __ATOMIC_RELAXED);
            return false;
        }
        if (__atomic_compare_exchange_n(&held_, &held, held + slots, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
            return true;
    }
}

void HostDeviceSlots::give(uint32_t slots)
{
    AV_SCOPE("HostDeviceSlots::give");
    if (!bounded() || slots == 0u) return;
    uint32_t held = __atomic_load_n(&held_, __ATOMIC_RELAXED);
    while (!__atomic_compare_exchange_n(&held_, &held, held > slots ? held - slots : 0u, true, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED)) {}
}

uint32_t HostDeviceSlots::held() const
{
    AV_SCOPE("HostDeviceSlots::held");
    return __atomic_load_n(&held_, __ATOMIC_RELAXED);
}

uint64_t HostDeviceSlots::refusals() const
{
    AV_SCOPE("HostDeviceSlots::refusals");
    return __atomic_load_n(&refusals_, __ATOMIC_RELAXED);
}

bool HostDeviceSlots::invariants_hold() const
{
    AV_SCOPE("HostDeviceSlots::invariants_hold");
    return held() <= capacity_;
}

}
