#include "icd/d3d/d3d9_level_caps.hpp"

#include "core/trace/function_scope.hpp"
#include "icd/d3d/caps_reply.hpp"

namespace av::d3d {

D3d9LevelCaps::D3d9LevelCaps()
{
    AV_SCOPE("D3d9LevelCaps::D3d9LevelCaps");
}

bool D3d9LevelCaps::knows(uint32_t type) const
{
    AV_SCOPE("D3d9LevelCaps::knows");
    return type >= first_type && type <= last_type;
}

Hresult D3d9LevelCaps::answer(uint32_t type, void *data, size_t bytes) const
{
    AV_SCOPE("D3d9LevelCaps::answer");
    const CapsReply reply(data, bytes);
    switch (type) {
    case architecture_info: {
        const uint32_t tile_based_deferred = 0u;
        return reply.give_whole(&tile_based_deferred, sizeof tile_based_deferred) ? hr_ok : hr_invalid_arg;
    }
    case shader_min_precision: {
        const uint32_t vertex_and_pixel[2] = {0u, 0u};
        return reply.give_whole(vertex_and_pixel, sizeof vertex_and_pixel) ? hr_ok : hr_invalid_arg;
    }
    case multiplane_overlay: {
        const uint32_t planes_and_groups[2] = {0u, 0u};
        return reply.give_whole(planes_and_groups, sizeof planes_and_groups) ? hr_ok : hr_invalid_arg;
    }
    case simple_instancing: {
        const uint32_t supported = 1u;
        return reply.give_whole(&supported, sizeof supported) ? hr_ok : hr_invalid_arg;
    }
    case marker: {
        const uint32_t no_marker_types = 0u;
        return reply.give_whole(&no_marker_types, sizeof no_marker_types) ? hr_ok : hr_invalid_arg;
    }
    case multiplane_overlay_filter_range:
    case multiplane_overlay_group:
    default: return hr_invalid_arg;
    }
}

}
