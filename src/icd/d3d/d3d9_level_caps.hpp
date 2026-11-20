#ifndef AV_D3D_D3D9_LEVEL_CAPS_HPP
#define AV_D3D_D3D9_LEVEL_CAPS_HPP

#include "icd/d3d/hresult.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av::d3d {

class D3d9LevelCaps {
public:
    static constexpr uint32_t interface_level = 0x4002u;
    static constexpr uint32_t architecture_info = 45u;
    static constexpr uint32_t shader_min_precision = 46u;
    static constexpr uint32_t multiplane_overlay = 47u;
    static constexpr uint32_t multiplane_overlay_filter_range = 48u;
    static constexpr uint32_t multiplane_overlay_group = 49u;
    static constexpr uint32_t simple_instancing = 50u;
    static constexpr uint32_t marker = 51u;
    static constexpr uint32_t first_type = 45u;
    static constexpr uint32_t last_type = 51u;

    D3d9LevelCaps();

    bool              knows(uint32_t type) const;
    [[nodiscard]] Hresult answer(uint32_t type, void *data, size_t bytes) const;
};

}

#endif
