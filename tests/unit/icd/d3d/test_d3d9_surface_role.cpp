#include "harness/framework/test_registry.hpp"
#include "icd/d3d/d3d9_surface_role.hpp"

#include <cstdint>

using av::d3d::D3d9SurfaceRole;
using Kind = av::d3d::D3d9SurfaceRole::Kind;

namespace {

constexpr std::uint32_t render_target = 0x81u;
constexpr std::uint32_t shared_render_target = 0x881u;
constexpr std::uint32_t shared_render_target_texture = 0x10881u;
constexpr std::uint32_t cross_adapter = 0x4u;
constexpr std::uint32_t video_memory = 2u;
constexpr std::uint32_t system_memory = 1u;
constexpr std::uint32_t x8r8g8b8 = 22u;
constexpr std::uint32_t a8r8g8b8 = 21u;
constexpr std::uint32_t a16b16g16r16f = 113u;

}

TEST(d3d9_surface_role, the_flip_model_swap_chain_of_a_render_adapter_is_two_cross_adapter_resources_lent_as_rows)
{
    const D3d9SurfaceRole on_display(render_target, 0u, video_memory, x8r8g8b8, 1u, 0u);
    CHECK(on_display.kind() == Kind::local);
    const D3d9SurfaceRole surface(shared_render_target, cross_adapter, video_memory, a8r8g8b8, 1u, 0u);
    CHECK(surface.kind() == Kind::across_adapters);
    CHECK(!surface.texture());
    const D3d9SurfaceRole texture(shared_render_target_texture, cross_adapter, video_memory, a8r8g8b8, 1u, 0u);
    CHECK(texture.kind() == Kind::across_adapters);
    CHECK(texture.texture());
}

TEST(d3d9_surface_role, the_same_resources_without_the_cross_adapter_flag_stay_shared_on_their_own_adapter)
{
    CHECK(D3d9SurfaceRole(shared_render_target, 0u, video_memory, a8r8g8b8, 1u, 0u).kind() == Kind::shared_image);
    const D3d9SurfaceRole texture(shared_render_target_texture, 0u, video_memory, x8r8g8b8, 1u, 0u);
    CHECK(texture.kind() == Kind::shared_image);
    CHECK(texture.texture());
    CHECK(D3d9SurfaceRole(shared_render_target, 0u, video_memory, a16b16g16r16f, 1u, 0u).kind() == Kind::shared);
    CHECK(D3d9SurfaceRole(shared_render_target, 0u, video_memory, a8r8g8b8, 1u, 2u).kind() == Kind::shared);
    CHECK(D3d9SurfaceRole(shared_render_target_texture, 0u, video_memory, x8r8g8b8, 3u, 0u).kind() == Kind::local);
    CHECK(D3d9SurfaceRole(shared_render_target, 0u, system_memory, x8r8g8b8, 1u, 0u).kind() == Kind::local);
    CHECK(D3d9SurfaceRole(0x800u, 0u, video_memory, x8r8g8b8, 1u, 0u).kind() == Kind::local);
    CHECK(D3d9SurfaceRole(0x800u | D3d9SurfaceRole::flag_vertex_buffer, 0u, video_memory, 0u, 1u, 0u).kind() ==
          Kind::local);
}

TEST(d3d9_surface_role, a_primary_is_a_primary_whatever_else_it_is)
{
    CHECK(D3d9SurfaceRole(D3d9SurfaceRole::flag_primary | render_target, 0u, video_memory, x8r8g8b8, 1u, 0u).kind() ==
          Kind::primary);
    CHECK(D3d9SurfaceRole(D3d9SurfaceRole::flag_primary | shared_render_target, cross_adapter, video_memory, x8r8g8b8,
                          2u, 0u)
              .kind() == Kind::primary);
}

TEST(d3d9_surface_role, a_cross_adapter_resource_that_cannot_be_one_surface_of_rows_is_refused_not_made_local)
{
    CHECK(D3d9SurfaceRole(shared_render_target_texture, cross_adapter, video_memory, a8r8g8b8, 2u, 0u).kind() ==
          Kind::refused);
    CHECK(D3d9SurfaceRole(shared_render_target, cross_adapter, system_memory, a8r8g8b8, 1u, 0u).kind() ==
          Kind::refused);
    CHECK(D3d9SurfaceRole(shared_render_target, cross_adapter, video_memory, a16b16g16r16f, 1u, 0u).kind() ==
          Kind::refused);
    CHECK(D3d9SurfaceRole(shared_render_target, cross_adapter, video_memory, a8r8g8b8, 1u, 4u).kind() ==
          Kind::refused);
    CHECK(D3d9SurfaceRole(0x800u | D3d9SurfaceRole::flag_z_buffer, cross_adapter, video_memory, a8r8g8b8, 1u, 0u)
              .kind() == Kind::refused);
    CHECK(D3d9SurfaceRole(0x800u | D3d9SurfaceRole::flag_cube_map, cross_adapter, video_memory, a8r8g8b8, 1u, 0u)
              .kind() == Kind::refused);
}
