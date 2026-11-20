#ifndef AV_RESOURCE_SIZE_LIMIT_HPP
#define AV_RESOURCE_SIZE_LIMIT_HPP

#include <stdint.h>

namespace av::d3d {

struct ResourceShape {
    bool     buffer;
    bool     volume;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mips;
    uint32_t layers;
};

class ResourceSizeLimit {
public:
    explicit ResourceSizeLimit(uint64_t bytes);

    bool exceeded_by(const ResourceShape &shape) const;

private:
    uint64_t bytes_;
};

}

#endif
