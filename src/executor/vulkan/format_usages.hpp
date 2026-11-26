#ifndef AV_FORMAT_USAGES_HPP
#define AV_FORMAT_USAGES_HPP

#include <stdint.h>

#include <vulkan/vulkan_core.h>

namespace av {

class FormatUsages {
public:
    static constexpr uint32_t pairings = 6u;

    explicit FormatUsages(const VkFormatProperties &host);

    VkFormatFeatureFlags feature(uint32_t pairing) const;
    VkImageUsageFlags    usage(uint32_t pairing) const;
    bool                 judges(VkImageTiling tiling) const;
    VkFormatFeatureFlags features(VkImageTiling tiling) const;
    bool                 admits(VkImageTiling tiling, VkImageUsageFlags usage) const;
    void                 drop(VkImageTiling tiling, VkFormatFeatureFlags feature);
    VkFormatFeatureFlags dropped(VkImageTiling tiling) const;
    void                 write(VkFormatProperties &into) const;

private:
    VkFormatFeatureFlags &held(VkImageTiling tiling);

    static constexpr VkFormatFeatureFlags features_[pairings] = {
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,          VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,       VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,           VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
    };
    static constexpr VkImageUsageFlags usages_[pairings] = {
        VK_IMAGE_USAGE_SAMPLED_BIT,          VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    VkFormatProperties   properties_;
    VkFormatFeatureFlags dropped_linear_;
    VkFormatFeatureFlags dropped_optimal_;
};

}

#endif
