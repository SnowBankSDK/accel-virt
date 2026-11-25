#include "device/renderer/image_export.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_plane.hpp"
#include "device/renderer/renderer_service.hpp"

namespace av {

ImageExport::ImageExport(RendererService &service, ContextPlane &plane, const uint8_t *gpu_uuid, uint64_t sequence,
                         uint32_t head, uint64_t key)
    : service_(service), plane_(plane), gpu_uuid_(gpu_uuid), sequence_(sequence), head_(head), key_(key),
      found_(false), image_{-1, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}
{
    AV_SCOPE("ImageExport::ImageExport");
}

void ImageExport::run()
{
    AV_SCOPE("ImageExport::run");
    found_ = plane_.export_image(gpu_uuid_, head_, key_, image_);
}

void ImageExport::finish(bool ran)
{
    AV_SCOPE("ImageExport::finish");
    service_.exported(*this, ran);
}

uint64_t ImageExport::sequence() const
{
    AV_SCOPE("ImageExport::sequence");
    return sequence_;
}

uint32_t ImageExport::head() const
{
    AV_SCOPE("ImageExport::head");
    return head_;
}

bool ImageExport::found() const
{
    AV_SCOPE("ImageExport::found");
    return found_;
}

const AvExportedImage &ImageExport::image() const
{
    AV_SCOPE("ImageExport::image");
    return image_;
}

}
