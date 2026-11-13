#ifndef AV_D3D_D3D9_SURFACE_ROLE_HPP
#define AV_D3D_D3D9_SURFACE_ROLE_HPP

#include <stdint.h>

namespace av::d3d {

class D3d9SurfaceRole {
public:
    enum class Kind : uint8_t { local, primary, shared, shared_image, across_adapters, refused };

    static constexpr uint32_t flag_render_target = 0x1u;
    static constexpr uint32_t flag_z_buffer = 0x2u;
    static constexpr uint32_t flag_primary = 0x8000u;
    static constexpr uint32_t flag_shared = 0x800u;
    static constexpr uint32_t flag_texture = 0x10000u;
    static constexpr uint32_t flag_cube_map = 0x20000u;
    static constexpr uint32_t flag_volume = 0x40000u;
    static constexpr uint32_t flag_vertex_buffer = 0x80000u;
    static constexpr uint32_t flag_index_buffer = 0x100000u;
    static constexpr uint32_t flag2_cross_adapter = 0x4u;
    static constexpr uint32_t pool_system_memory = 1u;
    static constexpr uint32_t format_x8r8g8b8 = 22u;
    static constexpr uint32_t format_a8r8g8b8 = 21u;

    D3d9SurfaceRole(uint32_t flags, uint32_t flags2, uint32_t pool, uint32_t format, uint32_t surfaces,
                    uint32_t multisample);

    Kind kind() const;
    bool texture() const;

private:
    Kind kind_;
    bool texture_;
};

}

#endif
