#include "harness/framework/test_registry.hpp"
#include "icd/d3d/resource_size_limit.hpp"

#include <cstdint>

using av::d3d::ResourceShape;
using av::d3d::ResourceSizeLimit;

namespace {
constexpr std::uint64_t mebibyte = std::uint64_t{1} << 20u;
constexpr std::uint64_t gibibyte = std::uint64_t{1} << 30u;
constexpr std::uint32_t rgba32_float = 2u;
constexpr std::uint32_t rgba8_unorm = 28u;

ResourceShape texture(std::uint32_t format, std::uint32_t width, std::uint32_t height, std::uint32_t mips = 1u,
                      std::uint32_t layers = 1u)
{
    return ResourceShape{false, false, format, width, height, 1u, mips, layers};
}
}

TEST(resource_size_limit, a_buffer_is_refused_above_the_limit_and_accepted_at_it)
{ const ResourceSizeLimit limit(2u * gibibyte);
  CHECK(!limit.exceeded_by(ResourceShape{true, false, 0u, 256u * mebibyte, 1u, 1u, 1u, 1u}));
  CHECK(!limit.exceeded_by(ResourceShape{true, false, 0u, 2u * gibibyte, 1u, 1u, 1u, 1u}));
  CHECK(limit.exceeded_by(ResourceShape{true, false, 0u, 2u * gibibyte + 1u, 1u, 1u, 1u, 1u})); }

TEST(resource_size_limit, a_texture_is_judged_by_the_bytes_of_all_its_subresources)
{ const ResourceSizeLimit limit(2u * gibibyte);
  CHECK(limit.exceeded_by(texture(rgba32_float, 16384u, 16384u)));
  CHECK(!limit.exceeded_by(texture(rgba32_float, 8192u, 8192u)));
  CHECK(!limit.exceeded_by(texture(rgba8_unorm, 16384u, 16384u)));
  CHECK(limit.exceeded_by(texture(rgba8_unorm, 16384u, 16384u, 1u, 9u)));
  CHECK(!limit.exceeded_by(texture(rgba32_float, 16384u, 8192u, 1u)));
  CHECK(limit.exceeded_by(texture(rgba32_float, 16384u, 8192u, 2u))); }

TEST(resource_size_limit, a_volume_counts_its_depth_and_not_its_layers)
{ const ResourceSizeLimit limit(2u * gibibyte);
  CHECK(limit.exceeded_by(ResourceShape{false, true, rgba32_float, 2048u, 2048u, 64u, 1u, 1u}));
  CHECK(!limit.exceeded_by(ResourceShape{false, true, rgba32_float, 1024u, 1024u, 64u, 1u, 1u})); }

TEST(resource_size_limit, a_small_texture_is_accepted_whatever_its_array_size_when_the_bytes_fit)
{ const ResourceSizeLimit limit(128u * mebibyte);
  CHECK(!limit.exceeded_by(texture(rgba8_unorm, 256u, 256u, 9u, 256u)));
  CHECK(limit.exceeded_by(texture(rgba8_unorm, 4096u, 4096u, 1u, 3u))); }
