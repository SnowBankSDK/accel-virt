#include "harness/framework/test_registry.hpp"
#include "proto/d3d/resource_footprint.hpp"
#include "proto/d3d/resource_misc.hpp"

#include <cstdint>

using av::ResourceFootprint;

namespace {

constexpr std::uint64_t granule = ResourceFootprint::granule;
constexpr std::uint32_t d3d9_a8r8g8b8 = 21u, d3d9_dxt1 = 0x31545844u;
constexpr std::uint32_t dxgi_b8g8r8a8 = 87u;

av::d3dproto::ResourceDesc buffer_desc(std::uint64_t bytes, std::uint32_t misc)
{
    av::d3dproto::ResourceDesc desc = {};
    desc.dimension = ResourceFootprint::buffer_dimension;
    desc.width = static_cast<std::uint32_t>(bytes);
    desc.height = 1u;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.sample_count = 1u;
    desc.misc_flags = misc;
    return desc;
}

}

TEST(resource_footprint, a_direct3d9_texture_and_the_direct3d11_texture_of_its_shape_take_the_same_bytes)
{
    av::d3dproto::ResourceDesc texture = {};
    texture.dimension = 3u;
    texture.width = 512u;
    texture.height = 256u;
    texture.depth_or_array = 1u;
    texture.mip_levels = 3u;
    texture.format = dxgi_b8g8r8a8;
    texture.sample_count = 1u;
    CHECK_EQ(ResourceFootprint(d3d9_a8r8g8b8, 512u, 256u, 1u, 1u, 3u, 0u).bytes(),
             ResourceFootprint(texture).bytes());
}

TEST(resource_footprint, a_direct3d9_full_chain_counts_every_level_and_rounds_up_to_the_granule)
{
    const std::uint64_t chain = ResourceFootprint(d3d9_a8r8g8b8, 1024u, 1024u, 1u, 1u, 0u, 0u).bytes();
    CHECK_EQ(chain, std::uint64_t{86} * granule);
    CHECK_EQ(ResourceFootprint(d3d9_a8r8g8b8, 1024u, 1024u, 1u, 6u, 1u, 0u).bytes(),
             std::uint64_t{6} << 22u);
    CHECK_EQ(ResourceFootprint(d3d9_dxt1, 256u, 256u, 1u, 1u, 1u, 0u).bytes(), granule);
    CHECK_EQ(ResourceFootprint(d3d9_dxt1, 512u, 512u, 1u, 1u, 1u, 0u).bytes(), std::uint64_t{128} << 10u);
    CHECK_EQ(ResourceFootprint(d3d9_a8r8g8b8, 256u, 256u, 1u, 1u, 1u, 4u).bytes(), std::uint64_t{1} << 20u);
    CHECK_EQ(ResourceFootprint(1u, 0u).bytes(), granule);
}

TEST(resource_footprint, a_tile_pool_is_leased_for_its_size_and_a_tiled_resource_for_nothing)
{
    const std::uint32_t pool = av::ResourceMisc::tile_pool_bit, tiled = av::ResourceMisc::tiled_bit;
    CHECK_EQ(ResourceFootprint(buffer_desc(4u * granule, pool)).leased(0u), 4u * granule);
    CHECK_EQ(ResourceFootprint(4u * granule, av::ResourceMisc::tile_pool_bit).leased(0u), 4u * granule);
    CHECK_EQ(ResourceFootprint(buffer_desc(std::uint64_t{1} << 30u, tiled)).leased(0u), std::uint64_t{0});
    CHECK_EQ(ResourceFootprint(buffer_desc(granule, 0u)).leased(1u), std::uint64_t{0});
}
