#include "executor/shim/d3d12_image_imports.hpp"

#include "core/log/driver_log.hpp"
#include "core/log/log_line.hpp"
#include "core/trace/function_scope.hpp"
#include "executor/owned_fd.hpp"
#include "executor/sharing/shared_image_shape.hpp"
#include "executor/shim/d3d_headers.hpp"
#include "proto/vulkan/exportable_image_info.hpp"

#include <stddef.h>

namespace av {

namespace {
constexpr GUID     import_data = {0x6a2c9e51, 0x3f08, 0x4d7b, {0xa1, 0x64, 0x2e, 0x95, 0xc8, 0x0f, 0x7d, 0x13}};
constexpr size_t   desc1_bytes = 64u;
constexpr uint64_t drm_format_mod_linear = 0u;
constexpr uint32_t bind_render_target = 0x20u;
constexpr uint32_t bind_depth_stencil = 0x40u;
constexpr uint32_t bind_unordered_access = 0x80u;
}

D3d12ImageImport::D3d12ImageImport(ImageRetirement &owner) : TextureHold(owner), image_()
{
    AV_SCOPE("D3d12ImageImport::D3d12ImageImport");
}

VulkanImage &D3d12ImageImport::image()
{
    AV_SCOPE("D3d12ImageImport::image");
    return image_;
}

D3d12ImageImports::D3d12ImageImports(DriverLog &lines)
    : lines_(lines), heap_(), images_(heap_), table_(), borrow_(nullptr), retired_(0u)
{
    AV_SCOPE("D3d12ImageImports::D3d12ImageImports");
}

D3d12ImageImports::~D3d12ImageImports()
{
    AV_SCOPE("D3d12ImageImports::~D3d12ImageImports");
    release();
}

bool D3d12ImageImports::connect(ID3D12Device *device)
{
    AV_SCOPE("D3d12ImageImports::connect");
    if (borrow_ != nullptr) return true;
    ID3D12DeviceExt1 *ext = nullptr;
    if (device == nullptr ||
        FAILED(device->QueryInterface(__uuidof(ID3D12DeviceExt1), reinterpret_cast<void **>(&ext))))
        return false;
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice         vulkan = VK_NULL_HANDLE;
    if (FAILED(ext->GetVulkanHandles(&instance, &physical, &vulkan)) || !table_.load(instance, physical, vulkan) ||
        !table_.imports_dma_bufs()) {
        table_.forget();
        ext->Release();
        return false;
    }
    borrow_ = ext;
    return true;
}

bool D3d12ImageImports::connected() const
{
    AV_SCOPE("D3d12ImageImports::connected");
    return borrow_ != nullptr;
}

void D3d12ImageImports::release()
{
    AV_SCOPE("D3d12ImageImports::release");
    images_.destroy_if([](const D3d12ImageImport &made) {
        const_cast<D3d12ImageImport &>(made).image().release();
        return true;
    });
    __atomic_store_n(&retired_, 0u, __ATOMIC_RELEASE);
    table_.forget();
    if (borrow_ != nullptr) borrow_->Release();
    borrow_ = nullptr;
}

ID3D12Resource *D3d12ImageImports::adopt(OwnedFd &descriptor, const SharedImageShape &shape)
{
    AV_SCOPE("D3d12ImageImports::adopt");
    if (borrow_ == nullptr || !descriptor.held()) return nullptr;
    collect();
    const uint64_t            modifier = shape.modifier();
    const ExportableImageInfo explicit_rows(shape.width(), shape.height(), 1u, 1u, 1u, shape.format(), shape.bind(),
                                            modifier, shape.offset(), shape.pitch());
    const ExportableImageInfo tiled(shape.width(), shape.height(), 1u, 1u, 1u, shape.format(), shape.bind(), &modifier,
                                    1u);
    const ExportableImageInfo &described = modifier == drm_format_mod_linear ? explicit_rows : tiled;
    if (!described.supported()) {
        LogLine(lines_).text("a shared image of format ").number(shape.format()).text(" has no Direct3D 12 view here").write();
        return nullptr;
    }
    D3d12ImageImport *made = nullptr;
    if (images_.create(made, *this) != Status::ok) return nullptr;
    if (!made->image().create(table_, described.info()) || !laid_out_as(made->image(), shape) ||
        !made->image().import_dma_buf(descriptor)) {
        lines_.write("the Direct3D 12 device did not take a shared image's memory");
        discard(*made);
        return nullptr;
    }
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = shape.width();
    desc.Height = shape.height();
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.Format = static_cast<DXGI_FORMAT>(shape.format());
    desc.SampleDesc = {1u, 0u};
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if ((shape.bind() & bind_render_target) != 0u) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if ((shape.bind() & bind_depth_stencil) != 0u) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if ((shape.bind() & bind_unordered_access) != 0u) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    constexpr size_t mip_region_at = offsetof(D3D12_RESOURCE_DESC, Flags) + sizeof(D3D12_RESOURCE_FLAGS);
    static_assert(mip_region_at + 3u * sizeof(UINT) <= desc1_bytes);
    alignas(D3D12_RESOURCE_DESC) uint8_t described_desc[desc1_bytes] = {};
    __builtin_memcpy(described_desc, &desc, mip_region_at);
    ID3D12Resource *resource = nullptr;
    if (FAILED(borrow_->CreateResourceFromBorrowedHandle(described_desc, reinterpret_cast<UINT64>(made->image().image()),
                                                         &resource)) ||
        resource == nullptr ||
        FAILED(resource->SetPrivateDataInterface(import_data, reinterpret_cast<IUnknown *>(
                                                                  static_cast<TextureHold *>(made))))) {
        lines_.write("vkd3d-proton made no resource over a shared image");
        if (resource != nullptr) resource->Release();
        if (!made->retired()) discard(*made);
        return nullptr;
    }
    return resource;
}

bool D3d12ImageImports::laid_out_as(const VulkanImage &image, const SharedImageShape &shape) const
{
    AV_SCOPE("D3d12ImageImports::laid_out_as");
    const VkSubresourceLayout laid = image.row_layout();
    if (image.modifier() == shape.modifier() && laid.offset == shape.offset() && laid.rowPitch == shape.pitch())
        return true;
    LogLine(lines_).text("a shared image is laid out differently on the Direct3D 12 device: modifier ")
        .hex(image.modifier(), 16u).text(" pitch ").number(laid.rowPitch).text(" for ").hex(shape.modifier(), 16u)
        .text(" pitch ").number(shape.pitch()).write();
    return false;
}

void D3d12ImageImports::retired()
{
    AV_SCOPE("D3d12ImageImports::retired");
    __atomic_fetch_add(&retired_, 1u, __ATOMIC_ACQ_REL);
}

uint32_t D3d12ImageImports::live() const
{
    AV_SCOPE("D3d12ImageImports::live");
    return images_.live();
}

void D3d12ImageImports::collect()
{
    AV_SCOPE("D3d12ImageImports::collect");
    if (__atomic_exchange_n(&retired_, 0u, __ATOMIC_ACQ_REL) == 0u) return;
    images_.destroy_if([](const D3d12ImageImport &made) {
        if (!made.retired()) return false;
        const_cast<D3d12ImageImport &>(made).image().release();
        return true;
    });
}

void D3d12ImageImports::discard(D3d12ImageImport &made)
{
    AV_SCOPE("D3d12ImageImports::discard");
    made.image().release();
    static_cast<void>(images_.destroy(&made));
}

}
