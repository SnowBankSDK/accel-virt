#ifndef AV_COMPOSITOR_DEVICE_HPP
#define AV_COMPOSITOR_DEVICE_HPP

#include "executor/shim/vulkan_device_table.hpp"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

namespace av {

class HostLifecycleGate;

class CompositorDevice {
public:
    CompositorDevice();
    ~CompositorDevice();
    CompositorDevice(const CompositorDevice &) = delete;
    CompositorDevice &operator=(const CompositorDevice &) = delete;

    [[nodiscard]] bool       open(const uint8_t *uuid, HostLifecycleGate *gate);
    bool                     opened() const;
    void                     close();
    const VulkanDeviceTable &table() const;

private:
    [[nodiscard]] bool create(VkPhysicalDevice physical, const VkDeviceCreateInfo &created);
    [[nodiscard]] bool destroy();

    VkInstance        instance_;
    VkDevice          device_;
    HostLifecycleGate *gate_;
    VulkanDeviceTable table_;
};

}

#endif
