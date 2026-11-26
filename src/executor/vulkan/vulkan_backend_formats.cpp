#include "executor/vulkan/format_usages.hpp"
#include "executor/vulkan/vulkan_backend.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

void VulkanBackend::agree_features(VkPhysicalDevice physical, VkFormat format, FormatUsages &usages)
{
    AV_SCOPE("VulkanBackend::agree_features");
    const VkImageTiling tilings[] = {VK_IMAGE_TILING_LINEAR, VK_IMAGE_TILING_OPTIMAL};
    for (const VkImageTiling tiling : tilings) {
        for (uint32_t pairing = 0u; pairing < FormatUsages::pairings; pairing++) {
            if ((usages.features(tiling) & usages.feature(pairing)) == 0u) continue;
            VkImageFormatProperties probed = {};
            if (RealVulkanDispatch::on_vkGetPhysicalDeviceImageFormatProperties(
                    physical, format, VK_IMAGE_TYPE_2D, tiling, usages.usage(pairing), 0u, &probed) != VK_SUCCESS)
                usages.drop(tiling, usages.feature(pairing));
        }
    }
}

bool VulkanBackend::format_admits(VkPhysicalDevice physical, VkFormat format, VkImageTiling tiling,
                                  VkImageUsageFlags usage)
{
    AV_SCOPE("VulkanBackend::format_admits");
    VkFormatProperties host = {};
    RealVulkanDispatch::on_vkGetPhysicalDeviceFormatProperties(physical, format, &host);
    FormatUsages usages(host);
    if (!usages.judges(tiling)) return true;
    agree_features(physical, format, usages);
    return usages.admits(tiling, usage);
}

void VulkanBackend::on_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                           VkFormatProperties *pFormatProperties)
{
    AV_SCOPE("VulkanBackend::on_vkGetPhysicalDeviceFormatProperties");
    RealVulkanDispatch::on_vkGetPhysicalDeviceFormatProperties(physicalDevice, format, pFormatProperties);
    if (pFormatProperties == nullptr) return;
    FormatUsages usages(*pFormatProperties);
    agree_features(physicalDevice, format, usages);
    usages.write(*pFormatProperties);
}

void VulkanBackend::on_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format,
                                                            VkFormatProperties2 *pFormatProperties)
{
    AV_SCOPE("VulkanBackend::on_vkGetPhysicalDeviceFormatProperties2");
    RealVulkanDispatch::on_vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
    if (pFormatProperties == nullptr) return;
    FormatUsages usages(pFormatProperties->formatProperties);
    agree_features(physicalDevice, format, usages);
    usages.write(pFormatProperties->formatProperties);
    for (auto *node = static_cast<VkBaseOutStructure *>(pFormatProperties->pNext); node != nullptr; node = node->pNext) {
        if (node->sType != VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) continue;
        auto *wide = reinterpret_cast<VkFormatProperties3 *>(node);
        wide->linearTilingFeatures &= ~VkFormatFeatureFlags2{usages.dropped(VK_IMAGE_TILING_LINEAR)};
        wide->optimalTilingFeatures &= ~VkFormatFeatureFlags2{usages.dropped(VK_IMAGE_TILING_OPTIMAL)};
    }
}

void VulkanBackend::on_vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format,
                                                               VkFormatProperties2 *pFormatProperties)
{
    AV_SCOPE("VulkanBackend::on_vkGetPhysicalDeviceFormatProperties2KHR");
    on_vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, pFormatProperties);
}

VkResult VulkanBackend::on_vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                    VkImageType type, VkImageTiling tiling,
                                                                    VkImageUsageFlags usage, VkImageCreateFlags flags,
                                                                    VkImageFormatProperties *pImageFormatProperties)
{
    AV_SCOPE("VulkanBackend::on_vkGetPhysicalDeviceImageFormatProperties");
    const VkResult answered = RealVulkanDispatch::on_vkGetPhysicalDeviceImageFormatProperties(
        physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
    if (answered != VK_SUCCESS || format_admits(physicalDevice, format, tiling, usage)) return answered;
    if (pImageFormatProperties != nullptr) *pImageFormatProperties = VkImageFormatProperties{};
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VkResult VulkanBackend::on_vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice                        physicalDevice,
                                                                     const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
                                                                     VkImageFormatProperties2 *pImageFormatProperties)
{
    AV_SCOPE("VulkanBackend::on_vkGetPhysicalDeviceImageFormatProperties2");
    const VkResult answered =
        RealVulkanDispatch::on_vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
    if (answered != VK_SUCCESS || pImageFormatInfo == nullptr ||
        format_admits(physicalDevice, pImageFormatInfo->format, pImageFormatInfo->tiling, pImageFormatInfo->usage))
        return answered;
    if (pImageFormatProperties != nullptr) pImageFormatProperties->imageFormatProperties = VkImageFormatProperties{};
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VkResult VulkanBackend::on_vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
    VkImageFormatProperties2 *pImageFormatProperties)
{
    AV_SCOPE("VulkanBackend::on_vkGetPhysicalDeviceImageFormatProperties2KHR");
    return on_vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

}
