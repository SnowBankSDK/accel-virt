#include "executor/vulkan/format_usages.hpp"
#include "harness/framework/test_registry.hpp"

using av::FormatUsages;

namespace {

VkFormatProperties nvidia_r8_unorm()
{ VkFormatProperties properties = {};
  properties.linearTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                    VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  properties.optimalTilingFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                     VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                     VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  properties.bufferFeatures = VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
  return properties; }

}

TEST(format_usages, a_usage_is_admitted_only_when_the_tiling_reports_its_feature)
{ const FormatUsages usages(nvidia_r8_unorm());
  CHECK(!usages.admits(VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
  CHECK(usages.admits(VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
  CHECK(usages.admits(VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
  CHECK(!usages.admits(VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT));
  CHECK(!usages.admits(VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)); }

TEST(format_usages, usages_without_a_feature_of_their_own_and_other_tilings_are_not_judged)
{ const FormatUsages usages(nvidia_r8_unorm());
  CHECK(usages.admits(VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT));
  CHECK(usages.admits(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
  CHECK(!usages.judges(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT));
  CHECK_EQ(usages.features(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT), 0u); }

TEST(format_usages, a_dropped_feature_is_no_longer_reported_or_admitted_and_is_remembered)
{ FormatUsages usages(nvidia_r8_unorm());
  usages.drop(VK_IMAGE_TILING_LINEAR, VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
  usages.drop(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  CHECK(!usages.admits(VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
  CHECK(usages.admits(VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
  CHECK_EQ(usages.dropped(VK_IMAGE_TILING_LINEAR), VkFormatFeatureFlags{VK_FORMAT_FEATURE_TRANSFER_DST_BIT});
  CHECK_EQ(usages.dropped(VK_IMAGE_TILING_OPTIMAL), 0u);
  CHECK_EQ(usages.dropped(VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT), 0u);
  VkFormatProperties written = {};
  usages.write(written);
  CHECK_EQ(written.linearTilingFeatures,
           VkFormatFeatureFlags{VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT});
  CHECK_EQ(written.optimalTilingFeatures, nvidia_r8_unorm().optimalTilingFeatures);
  CHECK_EQ(written.bufferFeatures, nvidia_r8_unorm().bufferFeatures); }

TEST(format_usages, each_usage_is_refused_once_the_feature_it_pairs_with_is_dropped)
{ VkFormatProperties every = {};
  every.linearTilingFeatures = ~0u;
  every.optimalTilingFeatures = ~0u;
  for (uint32_t pairing = 0u; pairing < FormatUsages::pairings; pairing++) {
      FormatUsages usages(every);
      CHECK(usages.admits(VK_IMAGE_TILING_OPTIMAL, usages.usage(pairing)));
      usages.drop(VK_IMAGE_TILING_OPTIMAL, usages.feature(pairing));
      CHECK(!usages.admits(VK_IMAGE_TILING_OPTIMAL, usages.usage(pairing)));
      CHECK(usages.admits(VK_IMAGE_TILING_LINEAR, usages.usage(pairing)));
      CHECK(usages.admits(VK_IMAGE_TILING_OPTIMAL, usages.usage((pairing + 1u) % FormatUsages::pairings))); }
  const FormatUsages usages(every);
  CHECK_EQ(usages.feature(FormatUsages::pairings), 0u);
  CHECK_EQ(usages.usage(FormatUsages::pairings), 0u);
  CHECK_EQ(usages.feature(2u), VkFormatFeatureFlags{VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT});
  CHECK_EQ(usages.usage(2u), VkImageUsageFlags{VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT}); }
