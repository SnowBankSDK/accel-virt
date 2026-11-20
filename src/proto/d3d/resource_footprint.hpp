#ifndef AV_RESOURCE_FOOTPRINT_HPP
#define AV_RESOURCE_FOOTPRINT_HPP

#include "proto/generated/d3d/d3d_wire_types.h"
#include "proto/resources/texel_layout.hpp"

#include <stdint.h>

namespace av {

class ResourceFootprint {
public:
    static constexpr uint64_t granule = uint64_t{64} << 10u;
    static constexpr uint32_t buffer_dimension = 1u;
    static constexpr uint32_t volume_dimension = 4u;
    static constexpr uint64_t direct3d9_device_bytes = uint64_t{16} << 20u;

    explicit ResourceFootprint(const d3dproto::ResourceDesc &desc);

    ResourceFootprint(uint32_t d3d9_format, uint32_t width, uint32_t height, uint32_t depth, uint32_t layers,
                      uint32_t levels, uint32_t samples);
    ResourceFootprint(uint64_t buffer_bytes, uint32_t misc);

    uint64_t bytes() const;
    uint64_t leased(uint64_t lent_memory) const;

private:
    ResourceFootprint(const TexelLayout &layout, bool buffer, uint64_t width, uint32_t height, uint32_t depth,
                      uint32_t layers, uint32_t levels, uint32_t samples, uint32_t misc);

    TexelLayout layout_;
    bool        buffer_;
    uint64_t    width_;
    uint32_t    height_;
    uint32_t    depth_;
    uint32_t    layers_;
    uint32_t    levels_;
    uint32_t    samples_;
    uint32_t    misc_;
};

}

#endif
