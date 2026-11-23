#include "executor/shim/host_surface_copy.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/shim/conversion_target.hpp"
#include "executor/shim/d3d_headers.hpp"

namespace av {

HostSurfaceCopy::HostSurfaceCopy() : fence_(nullptr), value_(0u), waits_(), converter_()
{
    AV_SCOPE("HostSurfaceCopy::HostSurfaceCopy");
}

HostSurfaceCopy::~HostSurfaceCopy()
{
    AV_SCOPE("HostSurfaceCopy::~HostSurfaceCopy");
    release();
}

void HostSurfaceCopy::release()
{
    AV_SCOPE("HostSurfaceCopy::release");
    if (fence_ != nullptr) fence_->Release();
    fence_ = nullptr;
    value_ = 0u;
    converter_.release();
}

bool HostSurfaceCopy::compatible(ID3D11Texture2D *source, ID3D11Texture2D *destination, uint32_t &width,
                                 uint32_t &height) const
{
    AV_SCOPE("HostSurfaceCopy::compatible");
    D3D11_TEXTURE2D_DESC from = {};
    D3D11_TEXTURE2D_DESC into = {};
    source->GetDesc(&from);
    destination->GetDesc(&into);
    if (from.Format != into.Format || from.SampleDesc.Count != 1u || into.SampleDesc.Count != 1u) return false;
    if (from.Format != DXGI_FORMAT_B8G8R8A8_UNORM && from.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
        from.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS)
        return false;
    width = from.Width < into.Width ? from.Width : into.Width;
    height = from.Height < into.Height ? from.Height : into.Height;
    return true;
}

ID3D11Texture2D *HostSurfaceCopy::converted(ID3D11Device5 *device, ID3D11DeviceContext4 *context,
                                            ID3D11Texture2D *source, ID3D11Texture2D *destination, uint32_t &width,
                                            uint32_t &height)
{
    AV_SCOPE("HostSurfaceCopy::converted");
    D3D11_TEXTURE2D_DESC from = {};
    D3D11_TEXTURE2D_DESC into = {};
    source->GetDesc(&from);
    destination->GetDesc(&into);
    const ConversionTarget target(from.Format, into.Format);
    if (from.SampleDesc.Count != 1u || into.SampleDesc.Count != 1u || !target.convertible() || target.raw_copy())
        return nullptr;
    width = from.Width < into.Width ? from.Width : into.Width;
    height = from.Height < into.Height ? from.Height : into.Height;
    return converter_.draw(device, context, source, 0u, HostFormatConverter::Rectangle{0u, 0u, width, height}, width,
                           height, target.format());
}

bool HostSurfaceCopy::fits(const AvCopyRegion &region, uint32_t width, uint32_t height) const
{
    AV_SCOPE("HostSurfaceCopy::fits");
    if (region.width == 0u || region.height == 0u) return false;
    return uint64_t{region.source_x} + region.width <= width && uint64_t{region.source_y} + region.height <= height &&
           uint64_t{region.destination_x} + region.width <= width &&
           uint64_t{region.destination_y} + region.height <= height;
}

bool HostSurfaceCopy::arm(ID3D11Device5 *device)
{
    AV_SCOPE("HostSurfaceCopy::arm");
    if (fence_ != nullptr) return true;
    value_ = 0u;
    return SUCCEEDED(device->CreateFence(0u, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence),
                                         reinterpret_cast<void **>(&fence_)));
}

bool HostSurfaceCopy::submit(ID3D11Device5 *device, ID3D11DeviceContext4 *context, ID3D11Texture2D *source,
                             ID3D11Texture2D *destination, const AvCopyRegion *regions, uint32_t count,
                             ID3D11Fence *&fence, uint64_t &value)
{
    AV_SCOPE("HostSurfaceCopy::submit");
    fence = nullptr;
    value = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (device == nullptr || context == nullptr || source == nullptr || destination == nullptr || regions == nullptr)
        return false;
    ID3D11Texture2D *from = source;
    if (!compatible(source, destination, width, height))
        from = converted(device, context, source, destination, width, height);
    if (from == nullptr || !arm(device)) return false;
    if (count == 0u) return false;
    for (uint32_t index = 0u; index < count; index++)
        if (!fits(regions[index], width, height)) return false;
    for (uint32_t index = 0u; index < count; index++) {
        const AvCopyRegion &region = regions[index];
        const D3D11_BOX     box = {region.source_x, region.source_y, 0u, region.source_x + region.width,
                                   region.source_y + region.height, 1u};
        context->CopySubresourceRegion(destination, 0u, region.destination_x, region.destination_y, 0u, from, 0u,
                                       &box);
    }
    value = ++value_;
    if (FAILED(context->Signal(fence_, value))) return false;
    context->Flush();
    fence_->AddRef();
    fence = fence_;
    return true;
}

bool HostSurfaceCopy::wait(ID3D11Fence *fence, uint64_t value, uint32_t wait_ms)
{
    AV_SCOPE("HostSurfaceCopy::wait");
    return waits_.wait(fence, value, wait_ms);
}

}
