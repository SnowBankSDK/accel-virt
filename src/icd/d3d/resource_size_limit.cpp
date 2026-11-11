#include "icd/d3d/resource_size_limit.hpp"

#include "core/trace/function_scope.hpp"
#include "proto/d3d/dxgi_format_layout.hpp"
#include "proto/resources/mip_extent.hpp"
#include "proto/resources/subresource_layout.hpp"

namespace av::d3d {

namespace {
constexpr uint64_t widest_texel_bytes = 16u;
constexpr uint64_t mip_chain_factor = 2u;
}

ResourceSizeLimit::ResourceSizeLimit(uint64_t bytes) : bytes_(bytes)
{
    AV_SCOPE("ResourceSizeLimit::ResourceSizeLimit");
}

bool ResourceSizeLimit::exceeded_by(const ResourceShape &shape) const
{
    AV_SCOPE("ResourceSizeLimit::exceeded_by");
    if (shape.buffer) return shape.width > bytes_;
    const uint64_t depth = shape.volume ? shape.depth : 1u;
    const uint64_t layers = shape.volume ? 1u : shape.layers;
    const uint64_t texels = static_cast<uint64_t>(shape.width) * (shape.height == 0u ? 1u : shape.height) * depth * layers;
    if (texels <= bytes_ / (widest_texel_bytes * mip_chain_factor)) return false;
    const DxgiFormatLayout layout(shape.format);
    const SubresourceLayout subresources(shape.mips, shape.layers, 1u);
    uint64_t total = 0u;
    for (uint32_t sub = 0u; sub < subresources.count() && total <= bytes_; sub++) {
        const MipExtent extent(shape.width, shape.height, shape.depth, subresources.mip_of(sub));
        total += static_cast<uint64_t>(layout.copied_row_bytes(static_cast<uint32_t>(extent.width()))) *
                 layout.rows(extent.height()) * extent.depth();
    }
    return total > bytes_;
}

}
