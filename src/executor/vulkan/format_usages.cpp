#include "executor/vulkan/format_usages.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

FormatUsages::FormatUsages(const VkFormatProperties &host)
    : properties_(host), dropped_linear_(0u), dropped_optimal_(0u)
{
    AV_SCOPE("FormatUsages::FormatUsages");
}

VkFormatFeatureFlags FormatUsages::feature(uint32_t pairing) const
{
    AV_SCOPE("FormatUsages::feature");
    return pairing < pairings ? features_[pairing] : 0u;
}

VkImageUsageFlags FormatUsages::usage(uint32_t pairing) const
{
    AV_SCOPE("FormatUsages::usage");
    return pairing < pairings ? usages_[pairing] : 0u;
}

bool FormatUsages::judges(VkImageTiling tiling) const
{
    AV_SCOPE("FormatUsages::judges");
    return tiling == VK_IMAGE_TILING_LINEAR || tiling == VK_IMAGE_TILING_OPTIMAL;
}

VkFormatFeatureFlags FormatUsages::features(VkImageTiling tiling) const
{
    AV_SCOPE("FormatUsages::features");
    if (tiling == VK_IMAGE_TILING_LINEAR) return properties_.linearTilingFeatures;
    if (tiling == VK_IMAGE_TILING_OPTIMAL) return properties_.optimalTilingFeatures;
    return 0u;
}

bool FormatUsages::admits(VkImageTiling tiling, VkImageUsageFlags usage) const
{
    AV_SCOPE("FormatUsages::admits");
    if (!judges(tiling)) return true;
    const VkFormatFeatureFlags held = features(tiling);
    for (uint32_t pairing = 0u; pairing < pairings; pairing++)
        if ((usage & usages_[pairing]) != 0u && (held & features_[pairing]) == 0u) return false;
    return true;
}

void FormatUsages::drop(VkImageTiling tiling, VkFormatFeatureFlags feature)
{
    AV_SCOPE("FormatUsages::drop");
    if (!judges(tiling)) return;
    held(tiling) &= ~feature;
    (tiling == VK_IMAGE_TILING_LINEAR ? dropped_linear_ : dropped_optimal_) |= feature;
}

VkFormatFeatureFlags FormatUsages::dropped(VkImageTiling tiling) const
{
    AV_SCOPE("FormatUsages::dropped");
    if (tiling == VK_IMAGE_TILING_LINEAR) return dropped_linear_;
    if (tiling == VK_IMAGE_TILING_OPTIMAL) return dropped_optimal_;
    return 0u;
}

void FormatUsages::write(VkFormatProperties &into) const
{
    AV_SCOPE("FormatUsages::write");
    into = properties_;
}

VkFormatFeatureFlags &FormatUsages::held(VkImageTiling tiling)
{
    AV_SCOPE("FormatUsages::held");
    return tiling == VK_IMAGE_TILING_LINEAR ? properties_.linearTilingFeatures : properties_.optimalTilingFeatures;
}

}
