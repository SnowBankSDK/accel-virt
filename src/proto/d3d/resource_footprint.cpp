#include "proto/d3d/resource_footprint.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/d3d/dxgi_format_layout.hpp"
#include "proto/d3d/resource_misc.hpp"
#include "proto/resources/mip_extent.hpp"
#include "proto/resources/subresource_layout.hpp"

namespace av {

ResourceFootprint::ResourceFootprint(const d3dproto::ResourceDesc &desc) : desc_(desc)
{
    AV_SCOPE("ResourceFootprint::ResourceFootprint");
}

uint64_t ResourceFootprint::bytes() const
{
    AV_SCOPE("ResourceFootprint::bytes");
    uint64_t total = desc_.width;
    if (desc_.dimension != buffer_dimension) {
        const bool              volume = desc_.dimension == volume_dimension;
        const uint32_t          depth = volume ? desc_.depth_or_array : 1u;
        const uint32_t          layers = volume ? 1u : (desc_.depth_or_array == 0u ? 1u : desc_.depth_or_array);
        const uint32_t          mips = desc_.mip_levels == 0u ? 1u : desc_.mip_levels;
        const DxgiFormatLayout  layout(desc_.format);
        const SubresourceLayout subresources(mips, layers, 1u);
        total = 0u;
        for (uint32_t sub = 0u; sub < subresources.count(); sub++) {
            const MipExtent extent(desc_.width, desc_.height == 0u ? 1u : desc_.height, depth, subresources.mip_of(sub));
            total += static_cast<uint64_t>(layout.copied_row_bytes(static_cast<uint32_t>(extent.width()))) *
                     layout.rows(extent.height()) * extent.depth();
        }
        total *= desc_.sample_count == 0u ? 1u : desc_.sample_count;
    }
    return total > UINT64_MAX - granule ? UINT64_MAX : (total + granule - 1u) & ~(granule - 1u);
}

uint64_t ResourceFootprint::leased(uint64_t lent_memory) const
{
    AV_SCOPE("ResourceFootprint::leased");
    constexpr uint32_t unleased = ResourceMisc::tiled_bit | ResourceMisc::tile_pool_bit;
    if (lent_memory != 0u || (desc_.misc_flags & unleased) != 0u) return 0u;
    return bytes();
}

}
