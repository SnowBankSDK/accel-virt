#include "executor/vulkan/dma_buf_import_type.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

DmaBufImportType::DmaBufImportType(const VkPhysicalDeviceMemoryProperties &memory) : memory_(memory)
{
    AV_SCOPE("DmaBufImportType::DmaBufImportType");
}

uint32_t DmaBufImportType::choose(uint32_t allowed) const
{
    AV_SCOPE("DmaBufImportType::choose");
    const VkMemoryPropertyFlags preference[] = {local, snooped, coherent, 0u};
    for (const VkMemoryPropertyFlags wanted : preference) {
        const uint32_t type = first(allowed, wanted);
        if (type != no_type) return type;
    }
    return no_type;
}

uint32_t DmaBufImportType::from_elsewhere(uint32_t allowed) const
{
    AV_SCOPE("DmaBufImportType::from_elsewhere");
    const uint32_t count = memory_.memoryTypeCount < no_type ? memory_.memoryTypeCount : no_type;
    uint32_t       kept = allowed;
    for (uint32_t type = 0u; type < count; type++)
        if ((memory_.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u) kept &= ~(1u << type);
    return kept != 0u ? kept : allowed;
}

uint32_t DmaBufImportType::first(uint32_t allowed, VkMemoryPropertyFlags wanted) const
{
    AV_SCOPE("DmaBufImportType::first");
    const uint32_t count = memory_.memoryTypeCount < no_type ? memory_.memoryTypeCount : no_type;
    for (uint32_t type = 0u; type < count; type++)
        if ((allowed & (1u << type)) != 0u && (memory_.memoryTypes[type].propertyFlags & wanted) == wanted) return type;
    return no_type;
}

}
