#include "executor/d3d/d3d9_row_surfaces.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/owned_fd.hpp"
#include "executor/shim/d3d11_host_device.hpp"
#include "executor/shim/d3d_headers.hpp"
#include "executor/sharing/shared_image_shape.hpp"
#include "proto/d3d/dxgi_format.hpp"
#include "executor/source_memory.hpp"
#include "proto/d3d/d3d9_present_format.hpp"
#include "proto/vulkan/exportable_image_info.hpp"

namespace av {

namespace {

constexpr GUID     image_data = {0x7a3e91c5, 0x2d48, 0x4b0f, {0xa6, 0x13, 0x9e, 0x52, 0xc7, 0x08, 0xf1, 0x6d}};
constexpr GUID     owner_data = {0x1c8f4e27, 0x6ab3, 0x4d95, {0xb2, 0x7e, 0x40, 0xd1, 0x93, 0x5f, 0x0a, 0xe8}};
constexpr uint64_t drm_format_mod_linear = 0u;
constexpr uint32_t bind_drawn_and_sampled = 0x28u;
constexpr uint32_t texel_bytes = 4u;
constexpr DWORD    most_targets = 4u;
constexpr DWORD    pixel_stages = 16u;

bool drawn_as_rows(uint32_t format)
{
    AV_SCOPE("drawn_as_rows");
    return format == static_cast<uint32_t>(D3DFMT_A8R8G8B8) || format == static_cast<uint32_t>(D3DFMT_X8R8G8B8);
}

}

D3d9RowSurfaces::Maker::Maker(BlockSource &blocks)
    : vulkan_(), interop_(nullptr), images_(blocks), refused_(false), forgotten_(false)
{
    AV_SCOPE("D3d9RowSurfaces::Maker::Maker");
}

D3d9RowSurfaces::D3d9RowSurfaces(BlockSource &blocks)
    : blocks_(blocks), makers_(blocks), maker_of_(blocks, Pool::paged), retired_(0u)
{
    AV_SCOPE("D3d9RowSurfaces::D3d9RowSurfaces");
}

D3d9RowSurfaces::~D3d9RowSurfaces()
{
    AV_SCOPE("D3d9RowSurfaces::~D3d9RowSurfaces");
    release();
}

Status D3d9RowSurfaces::make(IDirect3DDevice9 *device, uint32_t kind, uint32_t width, uint32_t height,
                             uint32_t format, const SourceMemory &memory, uint32_t pitch, IUnknown *&made)
{
    AV_SCOPE("D3d9RowSurfaces::make");
    made = nullptr;
    if (device == nullptr || (kind != kind_surface && kind != kind_texture) || width == 0u || height == 0u)
        return Status::invalid_parameter;
    if (!drawn_as_rows(format)) return Status::not_supported;
    const uint64_t row_bytes = uint64_t{width} * texel_bytes;
    if (pitch < row_bytes || pitch % texel_bytes != 0u || uint64_t{height - 1u} * pitch + row_bytes > memory.size())
        return Status::invalid_parameter;
    D3d9PresentFormat presented(0u);
    if (!presented.take_ddi(format)) return Status::not_supported;
    collect();
    Maker *const maker = maker_for(device);
    if (maker == nullptr) return Status::not_supported;
    const ExportableImageInfo described(width, height, 1u, 1u, 1u, presented.dxgi(), bind_drawn_and_sampled,
                                        drm_format_mod_linear, 0u, pitch);
    OwnedFd descriptor;
    if (!described.supported() || !memory.share(descriptor)) return Status::not_supported;
    return wrap(*maker, kind, width, height, format, described, descriptor, nullptr, made);
}

Status D3d9RowSurfaces::make_over(IDirect3DDevice9 *device, uint32_t kind, D3d11HostDevice &owner,
                                  ID3D11Texture2D *shared, IUnknown *&made)
{
    AV_SCOPE("D3d9RowSurfaces::make_over");
    made = nullptr;
    if (device == nullptr || shared == nullptr || (kind != kind_surface && kind != kind_texture))
        return Status::invalid_parameter;
    OwnedFd          descriptor;
    SharedImageShape shape;
    if (!owner.carry_image(shared, descriptor, shape)) return Status::not_supported;
    const uint32_t format = DxgiFormat(shape.format()).ddi_format();
    if (!drawn_as_rows(format)) return Status::not_supported;
    collect();
    Maker *const maker = maker_for(device);
    if (maker == nullptr) return Status::not_supported;
    const ExportableImageInfo described(shape.width(), shape.height(), 1u, 1u, 1u, shape.format(),
                                        bind_drawn_and_sampled, shape.modifier(), shape.offset(), shape.pitch());
    if (!described.supported()) return Status::not_supported;
    return wrap(*maker, kind, shape.width(), shape.height(), format, described, descriptor, shared, made);
}

Status D3d9RowSurfaces::wrap(Maker &maker_ref, uint32_t kind, uint32_t width, uint32_t height, uint32_t format,
                             const ExportableImageInfo &described, OwnedFd &descriptor, IUnknown *keep,
                             IUnknown *&made)
{
    AV_SCOPE("D3d9RowSurfaces::wrap");
    Maker *const maker = &maker_ref;
    ExportableImage *image = nullptr;
    if (maker->images_.create(image, *this) != Status::ok) return Status::no_memory;
    if (!image->image().import_dma_buf(maker->vulkan_.table(), described.info(), descriptor)) {
        image->free();
        static_cast<void>(maker->images_.destroy(image));
        return Status::not_supported;
    }
    image->laid_out_by(drm_format_mod_linear);
    D3D9VkExtImageDesc desc = {};
    desc.Type = kind == kind_texture ? D3DRTYPE_TEXTURE : D3DRTYPE_SURFACE;
    desc.Width = width;
    desc.Height = height;
    desc.Depth = 1u;
    desc.MipLevels = 1u;
    desc.Usage = D3DUSAGE_RENDERTARGET;
    desc.Format = static_cast<D3DFORMAT>(format);
    desc.Pool = D3DPOOL_DEFAULT;
    desc.MultiSample = D3DMULTISAMPLE_NONE;
    IDirect3DResource9 *resource = nullptr;
    if (FAILED(maker->interop_->CreateImageOver(&desc, image->image().image(), &resource)) || resource == nullptr) {
        idle(*maker);
        image->free();
        static_cast<void>(maker->images_.destroy(image));
        return Status::not_supported;
    }
    IUnknown *const hold = reinterpret_cast<IUnknown *>(static_cast<TextureHold *>(image));
    if (FAILED(resource->SetPrivateData(image_data, hold, sizeof(IUnknown *), D3DSPD_IUNKNOWN))) {
        resource->Release();
        idle(*maker);
        image->free();
        static_cast<void>(maker->images_.destroy(image));
        return Status::no_memory;
    }
    if (keep != nullptr && FAILED(resource->SetPrivateData(owner_data, keep, sizeof(IUnknown *), D3DSPD_IUNKNOWN))) {
        resource->Release();
        return Status::no_memory;
    }
    made = resource;
    return Status::ok;
}

D3d9RowSurfaces::Maker *D3d9RowSurfaces::maker_for(IDirect3DDevice9 *device)
{
    AV_SCOPE("D3d9RowSurfaces::maker_for");
    const uint64_t key = reinterpret_cast<uintptr_t>(device);
    Maker *const *known = maker_of_.find(key);
    if (known != nullptr) return (*known)->refused_ ? nullptr : *known;
    if (maker_of_.reserve(maker_of_.count() + 1u) != Status::ok) return nullptr;
    Maker *made = nullptr;
    if (makers_.create(made, blocks_) != Status::ok) return nullptr;
    uint32_t slot = 0u;
    static_cast<void>(maker_of_.insert(key, made, slot));
    if (made->vulkan_.connect(device) && made->vulkan_.table().imports_dma_bufs() &&
        SUCCEEDED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice1), reinterpret_cast<void **>(&made->interop_))))
        return made;
    close(*made);
    made->refused_ = true;
    return nullptr;
}

void D3d9RowSurfaces::idle(Maker &maker)
{
    AV_SCOPE("D3d9RowSurfaces::idle");
    if (maker.interop_ != nullptr) maker.interop_->WaitForIdle();
}

void D3d9RowSurfaces::unbind(Maker &maker)
{
    AV_SCOPE("D3d9RowSurfaces::unbind");
    IDirect3DDevice9 *const device = maker.vulkan_.device();
    if (device == nullptr || maker.images_.live() == 0u) return;
    IDirect3DSurface9 *back = nullptr;
    if (SUCCEEDED(device->GetBackBuffer(0u, 0u, D3DBACKBUFFER_TYPE_MONO, &back)) && back != nullptr) {
        static_cast<void>(device->SetRenderTarget(0u, back));
        back->Release();
    }
    for (DWORD target = 1u; target < most_targets; target++) static_cast<void>(device->SetRenderTarget(target, nullptr));
    for (DWORD stage = 0u; stage < pixel_stages; stage++) static_cast<void>(device->SetTexture(stage, nullptr));
    for (DWORD stage = D3DDMAPSAMPLER; stage <= D3DVERTEXTEXTURESAMPLER3; stage++)
        static_cast<void>(device->SetTexture(stage, nullptr));
}

void D3d9RowSurfaces::free_retired(Maker &maker)
{
    AV_SCOPE("D3d9RowSurfaces::free_retired");
    bool any = false;
    maker.images_.for_each([&any](const ExportableImage &image) { any = any || image.retired(); });
    if (!any) return;
    idle(maker);
    maker.images_.destroy_if([](const ExportableImage &image) {
        if (!image.retired()) return false;
        const_cast<ExportableImage &>(image).free();
        return true;
    });
}

void D3d9RowSurfaces::close(Maker &maker)
{
    AV_SCOPE("D3d9RowSurfaces::close");
    unbind(maker);
    idle(maker);
    maker.images_.destroy_if([](const ExportableImage &image) {
        if (!image.retired()) return false;
        const_cast<ExportableImage &>(image).free();
        return true;
    });
    maker.images_.for_each([](const ExportableImage &image) { const_cast<ExportableImage &>(image).free(); });
    if (maker.interop_ != nullptr) maker.interop_->Release();
    maker.interop_ = nullptr;
    maker.vulkan_.release();
}

void D3d9RowSurfaces::collect()
{
    AV_SCOPE("D3d9RowSurfaces::collect");
    if (__atomic_load_n(&retired_, __ATOMIC_RELAXED) == 0u) return;
    __atomic_store_n(&retired_, 0u, __ATOMIC_RELEASE);
    maker_of_.for_each([this](uint64_t, Maker *maker) { free_retired(*maker); });
    maker_of_.erase_if([this](uint64_t, Maker *maker) {
        if (!maker->forgotten_ || maker->images_.live() != 0u) return false;
        close(*maker);
        static_cast<void>(makers_.destroy(maker));
        return true;
    });
}

void D3d9RowSurfaces::submit(IDirect3DDevice9 *device)
{
    AV_SCOPE("D3d9RowSurfaces::submit");
    Maker *const *known = maker_of_.find(reinterpret_cast<uintptr_t>(device));
    if (known != nullptr && (*known)->interop_ != nullptr) (*known)->interop_->FlushCommands();
}

void D3d9RowSurfaces::forget_device(IDirect3DDevice9 *device)
{
    AV_SCOPE("D3d9RowSurfaces::forget_device");
    Maker *const *known = maker_of_.find(reinterpret_cast<uintptr_t>(device));
    if (known == nullptr) return;
    (*known)->forgotten_ = true;
    unbind(**known);
    __atomic_fetch_add(&retired_, 1u, __ATOMIC_ACQ_REL);
    collect();
}

void D3d9RowSurfaces::release()
{
    AV_SCOPE("D3d9RowSurfaces::release");
    maker_of_.for_each([this](uint64_t, Maker *maker) { close(*maker); });
    maker_of_.clear();
    makers_.destroy_if([](const Maker &maker) { return maker.images_.live() == 0u; });
    __atomic_store_n(&retired_, 0u, __ATOMIC_RELEASE);
}

void D3d9RowSurfaces::retired()
{
    AV_SCOPE("D3d9RowSurfaces::retired");
    __atomic_fetch_add(&retired_, 1u, __ATOMIC_ACQ_REL);
}

uint32_t D3d9RowSurfaces::live() const
{
    AV_SCOPE("D3d9RowSurfaces::live");
    uint32_t live = 0u;
    maker_of_.for_each([&live](uint64_t, Maker *maker) { live += maker->images_.live(); });
    return live;
}

}
