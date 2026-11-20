#ifndef AV_SHARED_IMAGE_SHAPE_HPP
#define AV_SHARED_IMAGE_SHAPE_HPP

#include <stdint.h>

namespace av {

class SharedImageShape {
public:
    SharedImageShape();
    SharedImageShape(uint32_t width, uint32_t height, uint32_t format, uint32_t pitch);
    SharedImageShape(uint32_t width, uint32_t height, uint32_t format, uint32_t pitch, uint64_t modifier,
                     uint64_t offset, uint32_t bind);

    bool     described() const;
    uint32_t width() const;
    uint32_t height() const;
    uint32_t format() const;
    uint32_t pitch() const;
    uint64_t modifier() const;
    uint64_t offset() const;
    uint32_t bind() const;

private:
    uint32_t width_;
    uint32_t height_;
    uint32_t format_;
    uint32_t pitch_;
    uint32_t bind_;
    uint64_t modifier_;
    uint64_t offset_;
};

}

#endif
