#include "harness/framework/test_registry.hpp"
#include "icd/d3d/d3d9_level_caps.hpp"

#include <cstdint>
#include <cstring>

using av::d3d::D3d9LevelCaps;

TEST(d3d9_level_caps, every_caps_type_the_interface_level_brings_is_answered_or_depends_on_overlay_planes_the_driver_has_none_of)
{
    const D3d9LevelCaps caps;
    std::uint32_t       overlay[2] = {7u, 7u};
    CHECK_EQ(caps.answer(D3d9LevelCaps::multiplane_overlay, overlay, sizeof overlay), av::d3d::hr_ok);
    CHECK_EQ(overlay[0], 0u);
    CHECK_EQ(overlay[1], 0u);
    for (std::uint32_t type = D3d9LevelCaps::first_type; type <= D3d9LevelCaps::last_type; type++) {
        CHECK(caps.knows(type));
        std::uint8_t   data[64];
        std::memset(data, 0xee, sizeof data);
        const bool needs_planes = type == D3d9LevelCaps::multiplane_overlay_filter_range ||
                                  type == D3d9LevelCaps::multiplane_overlay_group;
        std::size_t    answered = 0u;
        for (std::size_t bytes = 4u; bytes <= 8u && answered == 0u; bytes += 4u)
            if (caps.answer(type, data, bytes) == av::d3d::hr_ok) answered = bytes;
        CHECK_EQ(answered != 0u, !needs_planes);
        CHECK_EQ(data[answered], 0xeeu);
    }
    CHECK(!caps.knows(D3d9LevelCaps::first_type - 1u));
    CHECK(!caps.knows(D3d9LevelCaps::last_type + 1u));
}

TEST(d3d9_level_caps, the_answers_are_no_tiles_no_minimum_precision_instancing_and_no_markers)
{
    const D3d9LevelCaps caps;
    std::uint32_t       one = 9u, two[2] = {9u, 9u};
    CHECK_EQ(caps.answer(D3d9LevelCaps::architecture_info, &one, sizeof one), av::d3d::hr_ok);
    CHECK_EQ(one, 0u);
    CHECK_EQ(caps.answer(D3d9LevelCaps::shader_min_precision, two, sizeof two), av::d3d::hr_ok);
    CHECK_EQ(two[0], 0u);
    CHECK_EQ(two[1], 0u);
    CHECK_EQ(caps.answer(D3d9LevelCaps::simple_instancing, &one, sizeof one), av::d3d::hr_ok);
    CHECK_EQ(one, 1u);
    CHECK_EQ(caps.answer(D3d9LevelCaps::marker, &one, sizeof one), av::d3d::hr_ok);
    CHECK_EQ(one, 0u);
}

TEST(d3d9_level_caps, a_reply_buffer_too_small_or_absent_is_refused_and_left_alone)
{
    const D3d9LevelCaps caps;
    std::uint32_t       two[2] = {9u, 9u};
    CHECK_EQ(caps.answer(D3d9LevelCaps::shader_min_precision, two, 4u), av::d3d::hr_invalid_arg);
    CHECK_EQ(caps.answer(D3d9LevelCaps::architecture_info, two, 2u), av::d3d::hr_invalid_arg);
    CHECK_EQ(caps.answer(D3d9LevelCaps::marker, nullptr, 4u), av::d3d::hr_invalid_arg);
    CHECK_EQ(caps.answer(D3d9LevelCaps::multiplane_overlay_group, two, sizeof two), av::d3d::hr_invalid_arg);
    CHECK_EQ(two[0], 9u);
    CHECK_EQ(two[1], 9u);
}
