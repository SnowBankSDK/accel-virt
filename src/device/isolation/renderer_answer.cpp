#include "device/isolation/renderer_answer.hpp"

#include "core/trace/function_scope.hpp"
#include "device/host/host.hpp"

namespace av {

RendererAnswer::RendererAnswer(Host &host, const RendererMessageHeader &header, const uint8_t *body, int *descriptors,
                               uint32_t count)
    : host_(host), header_(header), body_(body), descriptors_(descriptors), count_(count)
{
    AV_SCOPE("RendererAnswer::RendererAnswer");
}

RendererMessageHeader::Kind RendererAnswer::kind() const
{
    AV_SCOPE("RendererAnswer::kind");
    return header_.kind();
}

uint64_t RendererAnswer::sequence() const
{
    AV_SCOPE("RendererAnswer::sequence");
    return header_.sequence();
}

int RendererAnswer::descriptor() const
{
    AV_SCOPE("RendererAnswer::descriptor");
    return count_ != 0u ? descriptors_[0] : -1;
}

void RendererAnswer::close() const
{
    AV_SCOPE("RendererAnswer::close");
    for (uint32_t index = 0u; index < count_; index++) host_.close_descriptor(descriptors_[index]);
}

}
