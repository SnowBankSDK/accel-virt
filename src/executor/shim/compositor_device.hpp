#ifndef AV_COMPOSITOR_DEVICE_HPP
#define AV_COMPOSITOR_DEVICE_HPP

#include "executor/shim/vulkan_device_table.hpp"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

namespace av {

class PipelineTeardownOrder;

class CompositorDevice {
public:
    CompositorDevice();
    ~CompositorDevice();
    CompositorDevice(const CompositorDevice &) = delete;
    CompositorDevice &operator=(const CompositorDevice &) = delete;

    [[nodiscard]] bool       open(const uint8_t *uuid, PipelineTeardownOrder &order);
    bool                     opened() const;
    void                     close();
    const VulkanDeviceTable &table() const;

private:
    VkInstance        instance_;
    VkDevice          device_;
    PipelineTeardownOrder *order_;
    VulkanDeviceTable table_;
};

}

#endif
