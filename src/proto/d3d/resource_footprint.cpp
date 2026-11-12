#include "proto/d3d/resource_footprint.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/d3d/d3d9_format_layout.hpp"
#include "proto/d3d/dxgi_format_layout.hpp"
#include "proto/d3d/resource_misc.hpp"
#include "proto/resources/mip_extent.hpp"
#include "proto/resources/subresource_layout.hpp"

namespace av {

namespace {

uint32_t at_least_one(uint32_t value)
{
    AV_SCOPE("at_least_one");
    return value == 0u ? 1u : value;
}

uint32_t full_chain(uint32_t width, uint32_t height, uint32_t depth)
{
    AV_SCOPE("full_chain");
    uint32_t edge = width > height ? width : height;
    edge = edge > depth ? edge : depth;
    uint32_t levels = 1u;
    while (edge > 1u) {
        edge >>= 1u;
        levels++;
    }
    return levels;
}

}

ResourceFootprint::ResourceFootprint(const d3dproto::ResourceDesc &desc)
    : ResourceFootprint(DxgiFormatLayout(desc.format), desc.dimension == buffer_dimension, desc.width,
                        at_least_one(desc.height),
                        desc.dimension == volume_dimension ? at_least_one(desc.depth_or_array) : 1u,
                        desc.dimension == volume_dimension ? 1u : at_least_one(desc.depth_or_array),
                        at_least_one(desc.mip_levels), at_least_one(desc.sample_count), desc.misc_flags)
{
    AV_SCOPE("ResourceFootprint::ResourceFootprint");
}

ResourceFootprint::ResourceFootprint(const TexelLayout &layout, bool buffer, uint64_t width, uint32_t height,
                                     uint32_t depth, uint32_t layers, uint32_t levels, uint32_t samples, uint32_t misc)
    : layout_(layout), buffer_(buffer), width_(width), height_(height), depth_(depth), layers_(layers),
      levels_(levels), samples_(samples), misc_(misc)
{
    AV_SCOPE("ResourceFootprint::ResourceFootprint");
}

ResourceFootprint::ResourceFootprint(uint32_t d3d9_format, uint32_t width, uint32_t height, uint32_t depth,
                                     uint32_t layers, uint32_t levels, uint32_t samples)
    : ResourceFootprint(D3d9FormatLayout(d3d9_format), false, width, at_least_one(height), at_least_one(depth),
                        at_least_one(layers),
                        levels == 0u ? full_chain(width, at_least_one(height), at_least_one(depth)) : levels,
                        samples < 2u ? 1u : samples, 0u)
{
    AV_SCOPE("ResourceFootprint::ResourceFootprint");
}

ResourceFootprint::ResourceFootprint(uint64_t buffer_bytes, uint32_t misc)
    : ResourceFootprint(D3d9FormatLayout(0u), true, buffer_bytes, 1u, 1u, 1u, 1u, 1u, misc)
{
    AV_SCOPE("ResourceFootprint::ResourceFootprint");
}

uint64_t ResourceFootprint::bytes() const
{
    AV_SCOPE("ResourceFootprint::bytes");
    uint64_t total = width_;
    if (!buffer_) {
        const SubresourceLayout subresources(levels_, layers_, 1u);
        total = 0u;
        for (uint32_t sub = 0u; sub < subresources.count(); sub++) {
            const MipExtent extent(width_, height_, depth_, subresources.mip_of(sub));
            total += static_cast<uint64_t>(layout_.copied_row_bytes(static_cast<uint32_t>(extent.width()))) *
                     layout_.rows(extent.height()) * extent.depth();
        }
        total *= samples_;
    }
    return total > UINT64_MAX - granule ? UINT64_MAX : (total + granule - 1u) & ~(granule - 1u);
}

uint64_t ResourceFootprint::leased(uint64_t lent_memory) const
{
    AV_SCOPE("ResourceFootprint::leased");
    constexpr uint32_t unleased = ResourceMisc::tiled_bit | ResourceMisc::tile_pool_bit;
    if (lent_memory != 0u || (misc_ & unleased) != 0u) return 0u;
    return bytes();
}

}
