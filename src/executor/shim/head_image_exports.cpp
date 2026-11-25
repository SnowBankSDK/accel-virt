#include "executor/shim/head_image_exports.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

HeadImageExports::HeadImageExports(DxvkVulkanDevice &vulkan) : vulkan_(vulkan), exports_()
{
    AV_SCOPE("HeadImageExports::HeadImageExports");
}

HeadImageExports::~HeadImageExports()
{
    AV_SCOPE("HeadImageExports::~HeadImageExports");
    for (ObjectStorage<HostImageExport> &head : exports_) head.destroy();
}

HostImageExport *HeadImageExports::of(uint32_t head)
{
    AV_SCOPE("HeadImageExports::of");
    if (head >= heads) return nullptr;
    ObjectStorage<HostImageExport> &held = exports_[head];
    if (!held.constructed() && held.construct(vulkan_) != Status::ok) return nullptr;
    return &held.object();
}

void HeadImageExports::release()
{
    AV_SCOPE("HeadImageExports::release");
    for (ObjectStorage<HostImageExport> &head : exports_)
        if (head.constructed()) head.object().release();
}

}
