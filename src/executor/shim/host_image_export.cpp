#include "executor/shim/host_image_export.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/shim/d3d_headers.hpp"
#include "executor/shim/dxvk_vulkan_device.hpp"
#include "executor/shim/exported_image_layout.hpp"
#include "proto/d3d/dxgi_format.hpp"


namespace av {

namespace {
constexpr uint64_t drm_format_mod_linear = 0u;

}

HostImageExport::HostImageExport(DxvkVulkanDevice &vulkan)
    : vulkan_(vulkan), slots_{}, retired_(), turn_(), fence_(nullptr), value_(0u), width_(0u), height_(0u), format_(0u),
      unavailable_(false)
{
    AV_SCOPE("HostImageExport::HostImageExport");
}

HostImageExport::~HostImageExport()
{
    AV_SCOPE("HostImageExport::~HostImageExport");
    release();
}

bool HostImageExport::make_image(Image &image)
{
    AV_SCOPE("HostImageExport::make_image");
    const uint64_t                                  linear = drm_format_mod_linear;
    const VkImageDrmFormatModifierListCreateInfoEXT modifiers = {
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT, nullptr, 1u, &linear};
    VkExternalMemoryImageCreateInfo external = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, &modifiers,
                                                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkImageCreateInfo created = {};
    created.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    created.pNext = &external;
    created.imageType = VK_IMAGE_TYPE_2D;
    created.format = static_cast<VkFormat>(DxgiFormat(format_).vk_format());
    created.extent = {width_, height_, 1u};
    created.mipLevels = 1u;
    created.arrayLayers = 1u;
    created.samples = VK_SAMPLE_COUNT_1_BIT;
    created.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    created.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    created.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!image.image_.make_exportable(vulkan_.table(), created) || !image.image_.export_dma_buf(image.fd_))
        return false;
    D3D11_TEXTURE2D_DESC1 described = {};
    described.Width = width_;
    described.Height = height_;
    described.MipLevels = 1u;
    described.ArraySize = 1u;
    described.Format = static_cast<DXGI_FORMAT>(format_);
    described.SampleDesc = {1u, 0u};
    described.Usage = D3D11_USAGE_DEFAULT;
    return vulkan_.wrap(described, image.image_.image(), image.texture_);
}

bool HostImageExport::arm(ID3D11Device5 *device)
{
    AV_SCOPE("HostImageExport::arm");
    if (fence_ != nullptr) return true;
    value_ = 0u;
    return SUCCEEDED(device->CreateFence(0u, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence),
                                         reinterpret_cast<void **>(&fence_)));
}

bool HostImageExport::vacant(uint32_t slot) const
{
    AV_SCOPE("HostImageExport::vacant");
    return slots_[slot].texture_ == nullptr && !retired_.retired(slot);
}

void HostImageExport::collect()
{
    AV_SCOPE("HostImageExport::collect");
    for (uint32_t slot = 0u; slot < RetiredSlots::capacity; slot++) {
        if (!retired_.retired(slot) || !retired_.done(slot, vulkan_)) continue;
        drop(slots_[slot]);
        retired_.freed(slot);
    }
}

bool HostImageExport::made() const
{
    AV_SCOPE("HostImageExport::made");
    for (uint32_t image = 0u; image < ExportTurn::images; image++)
        if (slots_[turn_.slot(image)].texture_ == nullptr) return false;
    return true;
}

void HostImageExport::retire_current()
{
    AV_SCOPE("HostImageExport::retire_current");
    for (uint32_t image = 0u; image < ExportTurn::images; image++) {
        const uint32_t slot = turn_.slot(image);
        if (slots_[slot].texture_ == nullptr || retired_.retire(slot, vulkan_)) continue;
        vulkan_.idle();
        drop(slots_[slot]);
    }
}

bool HostImageExport::free_slot(uint32_t &slot)
{
    AV_SCOPE("HostImageExport::free_slot");
    for (int pass = 0; pass < 2; pass++) {
        for (slot = 0u; slot < RetiredSlots::capacity; slot++)
            if (vacant(slot)) return true;
        vulkan_.idle();
        for (uint32_t held = 0u; held < RetiredSlots::capacity; held++) {
            if (!retired_.retired(held)) continue;
            drop(slots_[held]);
            retired_.freed(held);
        }
    }
    return false;
}

bool HostImageExport::make(uint32_t width, uint32_t height, uint32_t format)
{
    AV_SCOPE("HostImageExport::make");
    if (width == width_ && height == height_ && format == format_ && made()) return true;
    retire_current();
    collect();
    width_ = width;
    height_ = height;
    format_ = format;
    turn_.begin();
    for (uint32_t image = 0u; image < ExportTurn::images; image++) {
        uint32_t slot = 0u;
        if (!free_slot(slot)) return false;
        turn_.place(image, slot);
        if (make_image(slots_[slot])) continue;
        drop(slots_[slot]);
        return false;
    }
    return true;
}

bool HostImageExport::stage(ID3D11Device5 *device, ID3D11DeviceContext4 *context, ID3D11Texture2D *source,
                            uint32_t &slot, ID3D11Fence *&fence, uint64_t &value)
{
    AV_SCOPE("HostImageExport::stage");
    fence = nullptr;
    value = 0u;
    if (unavailable_) return false;
    D3D11_TEXTURE2D_DESC described = {};
    source->GetDesc(&described);
    if (!DxgiFormat(static_cast<uint32_t>(described.Format)).scanned_out() || described.SampleDesc.Count != 1u)
        return false;
    if (!vulkan_.connected() || !make(described.Width, described.Height, static_cast<uint32_t>(described.Format))) {
        unavailable_ = true;
        return false;
    }
    if (!arm(device)) return false;
    slot = turn_.next();
    context->CopySubresourceRegion(slots_[slot].texture_, 0u, 0u, 0u, 0u, source, 0u, nullptr);
    value = ++value_;
    if (FAILED(context->Signal(fence_, value))) return false;
    context->Flush();
    fence_->AddRef();
    fence = fence_;
    return true;
}

void HostImageExport::handed(uint32_t slot)
{
    AV_SCOPE("HostImageExport::handed");
    turn_.handed(slot);
}

bool HostImageExport::describe(uint32_t slot, AvExportedImage &image) const
{
    AV_SCOPE("HostImageExport::describe");
    const Image &shown = slots_[slot];
    image.slot = turn_.shown_as(slot);
    return shown.texture_ != nullptr && ExportedImageLayout(shown.image_, width_, height_, format_, drm_format_mod_linear)
        .describe(shown.fd_.fd(), image);
}

void HostImageExport::drop(Image &image)
{
    AV_SCOPE("HostImageExport::drop");
    if (image.texture_ != nullptr) image.texture_->Release();
    image.texture_ = nullptr;
    image.fd_.close();
    image.image_.release();
}

void HostImageExport::release()
{
    AV_SCOPE("HostImageExport::release");
    bool held = retired_.any();
    for (const Image &image : slots_) held = held || image.texture_ != nullptr;
    if (held) vulkan_.idle();
    for (uint32_t slot = 0u; slot < RetiredSlots::capacity; slot++) {
        drop(slots_[slot]);
        retired_.freed(slot);
    }
    if (fence_ != nullptr) fence_->Release();
    fence_ = nullptr;
    value_ = 0u;
}

}
