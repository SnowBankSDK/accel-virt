#ifndef AV_EXECUTOR_D3D9_ROW_SURFACES_HPP
#define AV_EXECUTOR_D3D9_ROW_SURFACES_HPP

#include "core/support/keyed_slots.hpp"
#include "core/support/object_pool.hpp"
#include "core/support/status.hpp"
#include "executor/shim/d3d9_vulkan_lane.hpp"
#include "executor/shim/exportable_image.hpp"
#include "executor/shim/image_retirement.hpp"

#include <stdint.h>

struct IDirect3DDevice9;
struct ID3D11Texture2D;
struct ID3D9VkInteropDevice1;
struct IUnknown;

namespace av {

class BlockSource;
class D3d11HostDevice;
class ExportableImageInfo;
class OwnedFd;
class SourceMemory;

class D3d9RowSurfaces final : public ImageRetirement {
public:
    static constexpr uint32_t kind_surface = 1u;
    static constexpr uint32_t kind_texture = 2u;

    explicit D3d9RowSurfaces(BlockSource &blocks);
    ~D3d9RowSurfaces();

    [[nodiscard]] Status make(IDirect3DDevice9 *device, uint32_t kind, uint32_t width, uint32_t height,
                              uint32_t format, const SourceMemory &memory, uint32_t pitch, IUnknown *&made);
    [[nodiscard]] Status make_over(IDirect3DDevice9 *device, uint32_t kind, D3d11HostDevice &owner,
                                   ID3D11Texture2D *shared, IUnknown *&made);
    void     submit(IDirect3DDevice9 *device);
    void     forget_device(IDirect3DDevice9 *device);
    void     collect();
    void     release();
    void     retired() override;
    uint32_t live() const;

private:
    struct Maker {
        explicit Maker(BlockSource &blocks);
        D3d9VulkanLane              vulkan_;
        ID3D9VkInteropDevice1      *interop_;
        ObjectPool<ExportableImage> images_;
        bool                        refused_;
        bool                        forgotten_;
    };

    Maker *maker_for(IDirect3DDevice9 *device);
    [[nodiscard]] Status wrap(Maker &maker, uint32_t kind, uint32_t width, uint32_t height, uint32_t format,
                              const ExportableImageInfo &described, OwnedFd &descriptor, IUnknown *keep,
                              IUnknown *&made);
    void   close(Maker &maker);
    void   idle(Maker &maker);
    void   unbind(Maker &maker);
    void   free_retired(Maker &maker);

    BlockSource                  &blocks_;
    ObjectPool<Maker>             makers_;
    KeyedSlots<uint64_t, Maker *> maker_of_;
    uint32_t                      retired_;
};

}

#endif
