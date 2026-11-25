#ifndef AV_IMAGE_EXPORT_HPP
#define AV_IMAGE_EXPORT_HPP

#include "device/abi/av_executor.h"
#include "device/threading/errand.hpp"

#include <stdint.h>

namespace av {

class ContextPlane;
class RendererService;

class ImageExport final : public Errand {
public:
    ImageExport(RendererService &service, ContextPlane &plane, const uint8_t *gpu_uuid, uint64_t sequence,
                uint32_t head, uint64_t key);
    ~ImageExport() = default;

    uint64_t               sequence() const;
    uint32_t               head() const;
    bool                   found() const;
    const AvExportedImage &image() const;

protected:
    void run() override;
    void finish(bool ran) override;

private:
    RendererService &service_;
    ContextPlane    &plane_;
    const uint8_t   *gpu_uuid_;
    uint64_t         sequence_;
    uint32_t         head_;
    uint64_t         key_;
    bool             found_;
    AvExportedImage  image_;
};

}

#endif
