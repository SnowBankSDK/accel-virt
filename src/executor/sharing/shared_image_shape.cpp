#include "executor/sharing/shared_image_shape.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

SharedImageShape::SharedImageShape()
    : width_(0u), height_(0u), format_(0u), pitch_(0u), bind_(0u), modifier_(0u), offset_(0u)
{
    AV_SCOPE("SharedImageShape::SharedImageShape");
}

SharedImageShape::SharedImageShape(uint32_t width, uint32_t height, uint32_t format, uint32_t pitch)
    : width_(width), height_(height), format_(format), pitch_(pitch), bind_(0u), modifier_(0u), offset_(0u)
{
    AV_SCOPE("SharedImageShape::SharedImageShape");
}

SharedImageShape::SharedImageShape(uint32_t width, uint32_t height, uint32_t format, uint32_t pitch,
                                   uint64_t modifier, uint64_t offset, uint32_t bind)
    : width_(width), height_(height), format_(format), pitch_(pitch), bind_(bind), modifier_(modifier),
      offset_(offset)
{
    AV_SCOPE("SharedImageShape::SharedImageShape");
}

bool SharedImageShape::described() const
{
    AV_SCOPE("SharedImageShape::described");
    return width_ != 0u && height_ != 0u && format_ != 0u && pitch_ != 0u;
}

uint32_t SharedImageShape::width() const
{
    AV_SCOPE("SharedImageShape::width");
    return width_;
}

uint32_t SharedImageShape::height() const
{
    AV_SCOPE("SharedImageShape::height");
    return height_;
}

uint32_t SharedImageShape::format() const
{
    AV_SCOPE("SharedImageShape::format");
    return format_;
}

uint32_t SharedImageShape::pitch() const
{
    AV_SCOPE("SharedImageShape::pitch");
    return pitch_;
}

uint64_t SharedImageShape::modifier() const
{
    AV_SCOPE("SharedImageShape::modifier");
    return modifier_;
}

uint64_t SharedImageShape::offset() const
{
    AV_SCOPE("SharedImageShape::offset");
    return offset_;
}

uint32_t SharedImageShape::bind() const
{
    AV_SCOPE("SharedImageShape::bind");
    return bind_;
}

}
