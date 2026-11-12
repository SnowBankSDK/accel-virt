#ifndef AV_D3D12_IMAGE_IMPORTS_HPP
#define AV_D3D12_IMAGE_IMPORTS_HPP

#include "core/support/object_pool.hpp"
#include "executor/shim/host_heap.hpp"
#include "executor/shim/image_retirement.hpp"
#include "executor/shim/texture_hold.hpp"
#include "executor/shim/vulkan_device_table.hpp"
#include "executor/shim/vulkan_image.hpp"

#include <stdint.h>

struct ID3D12Device;
struct ID3D12DeviceExt1;
struct ID3D12Resource;

namespace av {

class DriverLog;
class OwnedFd;
class SharedImageShape;

class D3d12ImageImport final : public TextureHold {
public:
    explicit D3d12ImageImport(ImageRetirement &owner);

    VulkanImage &image();

private:
    VulkanImage image_;
};

class D3d12ImageImports final : public ImageRetirement {
public:
    explicit D3d12ImageImports(DriverLog &lines);
    ~D3d12ImageImports();
    D3d12ImageImports(const D3d12ImageImports &) = delete;
    D3d12ImageImports &operator=(const D3d12ImageImports &) = delete;

    [[nodiscard]] bool connect(ID3D12Device *device);
    bool               connected() const;
    void               release();
    ID3D12Resource    *adopt(OwnedFd &descriptor, const SharedImageShape &shape);
    void               retired() override;
    uint32_t           live() const;

private:
    void collect();
    void discard(D3d12ImageImport &made);
    bool laid_out_as(const VulkanImage &image, const SharedImageShape &shape) const;

    DriverLog                    &lines_;
    HostHeap                      heap_;
    ObjectPool<D3d12ImageImport>  images_;
    VulkanDeviceTable             table_;
    ID3D12DeviceExt1             *borrow_;
    uint32_t                      retired_;
};

}

#endif
