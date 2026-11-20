#ifndef AV_HOST_DEVICE_SLOTS_HPP
#define AV_HOST_DEVICE_SLOTS_HPP

#include <stdint.h>

#include <vulkan/vulkan_core.h>

namespace av {

class HostDeviceSlots {
public:
    static constexpr uint32_t most_families = 32u;
    static constexpr uint32_t context_slots = 1u;

    HostDeviceSlots();
    HostDeviceSlots(const HostDeviceSlots &) = delete;
    HostDeviceSlots &operator=(const HostDeviceSlots &) = delete;

    void     driven_by(VkDriverId driver);
    bool     bounded() const;
    uint32_t capacity() const;
    uint32_t device_slots(const VkQueueFamilyProperties *families, uint32_t family_count,
                          const VkDeviceCreateInfo &create) const;

    [[nodiscard]] bool admit(VkPhysicalDevice physical, const VkDeviceCreateInfo &create, uint32_t &slots);
    [[nodiscard]] bool take(uint32_t slots);
    void               give(uint32_t slots);

    uint32_t held() const;
    uint64_t refusals() const;

    bool invariants_hold() const;

private:
    uint32_t capacity_;
    uint32_t device_base_;
    uint32_t graphics_queue_;
    uint32_t other_queue_;
    uint32_t held_;
    uint64_t refusals_;
};

}

#endif
