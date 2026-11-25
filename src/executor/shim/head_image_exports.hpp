#ifndef AV_HEAD_IMAGE_EXPORTS_HPP
#define AV_HEAD_IMAGE_EXPORTS_HPP

#include "core/support/object_storage.hpp"
#include "executor/shim/host_image_export.hpp"

#include <stdint.h>

namespace av {

class DxvkVulkanDevice;

class HeadImageExports {
public:
    static constexpr uint32_t heads = 16u;

    explicit HeadImageExports(DxvkVulkanDevice &vulkan);
    ~HeadImageExports();
    HeadImageExports(const HeadImageExports &) = delete;
    HeadImageExports &operator=(const HeadImageExports &) = delete;

    HostImageExport *of(uint32_t head);
    void             release();

private:
    DxvkVulkanDevice              &vulkan_;
    ObjectStorage<HostImageExport> exports_[heads];
};

}

#endif
