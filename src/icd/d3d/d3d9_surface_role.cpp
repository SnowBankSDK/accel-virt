#include "icd/d3d/d3d9_surface_role.hpp"

#include "core/trace/function_scope.hpp"

namespace av::d3d {

D3d9SurfaceRole::D3d9SurfaceRole(uint32_t flags, uint32_t flags2, uint32_t pool, uint32_t format, uint32_t surfaces,
                                 uint32_t multisample)
    : kind_(Kind::local), texture_((flags & flag_texture) != 0u)
{
    AV_SCOPE("D3d9SurfaceRole::D3d9SurfaceRole");
    constexpr uint32_t not_one_surface = flag_cube_map | flag_volume | flag_vertex_buffer | flag_index_buffer;
    const bool single = surfaces == 1u && (flags & not_one_surface) == 0u;
    const bool colour = (flags & flag_z_buffer) == 0u;
    const bool in_video_memory = pool != pool_system_memory;
    const bool rows = format == format_a8r8g8b8 || format == format_x8r8g8b8;
    const bool one_image = single && colour && in_video_memory && rows && multisample == 0u;
    if ((flags & flag_primary) != 0u) {
        kind_ = Kind::primary;
    } else if ((flags2 & flag2_cross_adapter) != 0u) {
        kind_ = one_image ? Kind::across_adapters : Kind::refused;
    } else if ((flags & flag_shared) != 0u && (flags & flag_render_target) != 0u && single && in_video_memory) {
        kind_ = one_image ? Kind::shared_image : Kind::shared;
    }
}

D3d9SurfaceRole::Kind D3d9SurfaceRole::kind() const
{
    AV_SCOPE("D3d9SurfaceRole::kind");
    return kind_;
}

bool D3d9SurfaceRole::texture() const
{
    AV_SCOPE("D3d9SurfaceRole::texture");
    return texture_;
}

}
