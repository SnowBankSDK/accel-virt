#ifndef AV_RESOURCE_FOOTPRINT_HPP
#define AV_RESOURCE_FOOTPRINT_HPP

#include "proto/generated/d3d/d3d_wire_types.h"

#include <stdint.h>

namespace av {

class ResourceFootprint {
public:
    static constexpr uint64_t granule = uint64_t{64} << 10u;
    static constexpr uint32_t buffer_dimension = 1u;
    static constexpr uint32_t volume_dimension = 4u;

    explicit ResourceFootprint(const d3dproto::ResourceDesc &desc);

    uint64_t bytes() const;
    uint64_t leased(uint64_t lent_memory) const;

private:
    d3dproto::ResourceDesc desc_;
};

}

#endif
