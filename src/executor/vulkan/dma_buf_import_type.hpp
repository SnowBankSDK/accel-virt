#ifndef AV_DMA_BUF_IMPORT_TYPE_HPP
#define AV_DMA_BUF_IMPORT_TYPE_HPP

#include <stdint.h>

#include <vulkan/vulkan_core.h>

namespace av {

class DmaBufImportType {
public:
    static constexpr uint32_t no_type = VK_MAX_MEMORY_TYPES;

    explicit DmaBufImportType(const VkPhysicalDeviceMemoryProperties &memory);
    DmaBufImportType(const DmaBufImportType &) = delete;
    DmaBufImportType &operator=(const DmaBufImportType &) = delete;

    [[nodiscard]] uint32_t choose(uint32_t allowed) const;
    [[nodiscard]] uint32_t from_elsewhere(uint32_t allowed) const;

private:
    static constexpr VkMemoryPropertyFlags local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    static constexpr VkMemoryPropertyFlags coherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    static constexpr VkMemoryPropertyFlags snooped = coherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    uint32_t first(uint32_t allowed, VkMemoryPropertyFlags wanted) const;

    const VkPhysicalDeviceMemoryProperties &memory_;
};

}

#endif
