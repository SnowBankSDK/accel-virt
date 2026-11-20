#include "av_line_coverage.h"
#include "executor/vulkan/spirv_validator.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>

namespace {

constexpr std::uint32_t vs_words[] = {
    0x07230203u, 0x00010300u, 0x00070000u, 0x0000001au, 0x00000000u, 0x00020011u, 0x00000001u, 0x00020011u,
    0x00000020u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x000c000fu, 0x00000000u, 0x00000001u, 0x6e69616du,
    0x00000000u, 0x00000002u, 0x00000003u, 0x00000004u, 0x00000005u, 0x00000006u, 0x00000007u, 0x00000008u,
    0x00040047u, 0x00000002u, 0x0000000bu, 0x00000000u, 0x00040047u, 0x00000003u, 0x0000000bu, 0x00000003u,
    0x00040047u, 0x00000007u, 0x0000000bu, 0x00000001u, 0x00040047u, 0x00000004u, 0x0000001eu, 0x00000000u,
    0x00040047u, 0x00000005u, 0x0000001eu, 0x00000001u, 0x00030047u, 0x00000005u, 0x0000000eu, 0x00040047u,
    0x00000006u, 0x0000001eu, 0x00000002u, 0x00040047u, 0x00000008u, 0x0000001eu, 0x00000000u, 0x00020013u,
    0x00000009u, 0x00030021u, 0x0000000au, 0x00000009u, 0x00030016u, 0x0000000bu, 0x00000020u, 0x00040015u,
    0x0000000cu, 0x00000020u, 0x00000000u, 0x00040017u, 0x0000000du, 0x0000000bu, 0x00000004u, 0x00040017u,
    0x0000000eu, 0x0000000bu, 0x00000002u, 0x0004002bu, 0x0000000cu, 0x0000000fu, 0x00000002u, 0x0004001cu,
    0x00000010u, 0x0000000bu, 0x0000000fu, 0x0004001cu, 0x00000011u, 0x0000000eu, 0x0000000fu, 0x00040020u,
    0x00000012u, 0x00000003u, 0x0000000du, 0x00040020u, 0x00000013u, 0x00000003u, 0x00000010u, 0x00040020u,
    0x00000014u, 0x00000003u, 0x0000000cu, 0x00040020u, 0x00000015u, 0x00000003u, 0x00000011u, 0x00040020u,
    0x00000016u, 0x00000003u, 0x0000000bu, 0x00040020u, 0x00000017u, 0x00000001u, 0x0000000du, 0x0004003bu,
    0x00000012u, 0x00000002u, 0x00000003u, 0x0004003bu, 0x00000013u, 0x00000003u, 0x00000003u, 0x0004003bu,
    0x00000012u, 0x00000004u, 0x00000003u, 0x0004003bu, 0x00000014u, 0x00000005u, 0x00000003u, 0x0004003bu,
    0x00000015u, 0x00000006u, 0x00000003u, 0x0004003bu, 0x00000016u, 0x00000007u, 0x00000003u, 0x0004003bu,
    0x00000017u, 0x00000008u, 0x00000001u, 0x00050036u, 0x00000009u, 0x00000001u, 0x00000000u, 0x0000000au,
    0x000200f8u, 0x00000018u, 0x0004003du, 0x0000000du, 0x00000019u, 0x00000008u, 0x0003003eu, 0x00000002u,
    0x00000019u, 0x0003003eu, 0x00000004u, 0x00000019u, 0x000100fdu, 0x00010038u,
};

constexpr std::uint32_t fs_words[] = {
    0x07230203u, 0x00010000u, 0x0008000bu, 0x00000019u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0008000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000008u, 0x00000012u, 0x00000017u,
    0x00030010u, 0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u,
    0x6e69616du, 0x00000000u, 0x00030005u, 0x00000008u, 0x00006469u, 0x00030005u, 0x00000012u, 0x0000006fu,
    0x00040005u, 0x00000017u, 0x6f6c6f63u, 0x00000072u, 0x00030047u, 0x00000008u, 0x0000000eu, 0x00040047u,
    0x00000008u, 0x0000001eu, 0x00000001u, 0x00040047u, 0x00000012u, 0x0000001eu, 0x00000000u, 0x00040047u,
    0x00000017u, 0x0000001eu, 0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u,
    0x00040015u, 0x00000006u, 0x00000020u, 0x00000000u, 0x00040020u, 0x00000007u, 0x00000001u, 0x00000006u,
    0x0004003bu, 0x00000007u, 0x00000008u, 0x00000001u, 0x0004002bu, 0x00000006u, 0x0000000au, 0x00000003u,
    0x00020014u, 0x0000000bu, 0x00030016u, 0x0000000fu, 0x00000020u, 0x00040017u, 0x00000010u, 0x0000000fu,
    0x00000004u, 0x00040020u, 0x00000011u, 0x00000003u, 0x00000010u, 0x0004003bu, 0x00000011u, 0x00000012u,
    0x00000003u, 0x0004002bu, 0x0000000fu, 0x00000013u, 0x3f800000u, 0x0007002cu, 0x00000010u, 0x00000014u,
    0x00000013u, 0x00000013u, 0x00000013u, 0x00000013u, 0x00040020u, 0x00000016u, 0x00000001u, 0x00000010u,
    0x0004003bu, 0x00000016u, 0x00000017u, 0x00000001u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003du, 0x00000006u, 0x00000009u, 0x00000008u, 0x000500aau,
    0x0000000bu, 0x0000000cu, 0x00000009u, 0x0000000au, 0x000300f7u, 0x0000000eu, 0x00000000u, 0x000400fau,
    0x0000000cu, 0x0000000du, 0x0000000eu, 0x000200f8u, 0x0000000du, 0x0003003eu, 0x00000012u, 0x00000014u,
    0x000100fdu, 0x000200f8u, 0x0000000eu, 0x0004003du, 0x00000010u, 0x00000018u, 0x00000017u, 0x0003003eu,
    0x00000012u, 0x00000018u, 0x000100fdu, 0x00010038u,
};

constexpr std::uint32_t layer_words[] = {
    0x07230203u, 0x00010300u, 0x00070000u, 0x0000000cu, 0x00000000u, 0x00020011u, 0x00000001u, 0x00020011u,
    0x00000045u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0007000fu, 0x00000000u, 0x00000001u, 0x6e69616du,
    0x00000000u, 0x00000002u, 0x00000003u, 0x00040047u, 0x00000002u, 0x0000000bu, 0x00000000u, 0x00040047u,
    0x00000003u, 0x0000000bu, 0x00000009u, 0x00020013u, 0x00000004u, 0x00030021u, 0x00000005u, 0x00000004u,
    0x00030016u, 0x00000006u, 0x00000020u, 0x00040015u, 0x00000007u, 0x00000020u, 0x00000000u, 0x00040017u,
    0x00000008u, 0x00000006u, 0x00000004u, 0x00040020u, 0x00000009u, 0x00000003u, 0x00000008u, 0x00040020u,
    0x0000000au, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000009u, 0x00000002u, 0x00000003u, 0x0004003bu,
    0x0000000au, 0x00000003u, 0x00000003u, 0x00050036u, 0x00000004u, 0x00000001u, 0x00000000u, 0x00000005u,
    0x000200f8u, 0x0000000bu, 0x000100fdu, 0x00010038u,
};

constexpr std::uint32_t novec_words[] = {
    0x07230203u, 0x00010000u, 0x0008000bu, 0x00000012u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00000008u, 0x0000000cu, 0x00030010u,
    0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du,
    0x00000000u, 0x00030005u, 0x00000008u, 0x0000006fu, 0x00040005u, 0x0000000cu, 0x6f6c6f63u, 0x00000072u,
    0x00040047u, 0x00000008u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x0000000cu, 0x0000001eu, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u,
    0x00000000u, 0x00040020u, 0x00000007u, 0x00000003u, 0x00000006u, 0x0004003bu, 0x00000007u, 0x00000008u,
    0x00000003u, 0x00030016u, 0x00000009u, 0x00000020u, 0x00040017u, 0x0000000au, 0x00000009u, 0x00000004u,
    0x00040020u, 0x0000000bu, 0x00000001u, 0x0000000au, 0x0004003bu, 0x0000000bu, 0x0000000cu, 0x00000001u,
    0x0004002bu, 0x00000006u, 0x0000000du, 0x00000000u, 0x00040020u, 0x0000000eu, 0x00000001u, 0x00000009u,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x00050041u,
    0x0000000eu, 0x0000000fu, 0x0000000cu, 0x0000000du, 0x0004003du, 0x00000009u, 0x00000010u, 0x0000000fu,
    0x0004006du, 0x00000006u, 0x00000011u, 0x00000010u, 0x0003003eu, 0x00000008u, 0x00000011u, 0x000100fdu,
    0x00010038u,
};

struct Words {
    std::uint32_t *data = nullptr;
    std::size_t    count = 0u;

    Words() = default;
    Words(const Words &) = delete;
    Words &operator=(const Words &) = delete;
    ~Words() { std::free(data); }
};

bool valid(const Words &words)
{
    av::SpirvValidator validator;
    return validator.check(words.data, words.count * sizeof(std::uint32_t)) == av::SpirvValidator::Verdict::valid;
}

std::uint32_t inputs_of(const std::uint32_t *words, std::size_t count)
{
    av_lc_varying varyings[32] = {};
    std::uint32_t found = 0u;
    CHECK_EQ(av_lc_reflect(words, count, AV_LC_SC_INPUT, varyings, 32u, &found), int(AV_LC_OK));
    return found;
}

}

TEST(line_coverage_spirv, the_outputs_of_the_last_vertex_stage_are_described_with_their_types_and_locations)
{
    av_lc_varying varyings[32] = {};
    std::uint32_t found = 0u;
    CHECK_EQ(av_lc_reflect(vs_words, std::size(vs_words), AV_LC_SC_OUTPUT, varyings, 32u, &found), int(AV_LC_OK));
    CHECK_EQ(found, 5u);
    CHECK_EQ(varyings[0].kind, std::uint32_t(AV_LC_KIND_POSITION));
    CHECK_EQ(varyings[1].kind, std::uint32_t(AV_LC_KIND_CLIP));
    CHECK_EQ(varyings[1].elements, 2u);
    CHECK_EQ(varyings[3].scalar, std::uint32_t(AV_LC_UINT));
    CHECK_EQ(varyings[3].location, 1u);
    CHECK_EQ(varyings[4].columns, 2u);
    CHECK_EQ(varyings[4].elements, 2u);
    CHECK_EQ(av_lc_next_location(varyings, found), 4u);
}

TEST(line_coverage_spirv, the_geometry_stage_built_for_lines_and_lines_with_adjacency_is_valid_spirv)
{
    av_lc_varying varyings[32] = {};
    std::uint32_t found = 0u;
    CHECK_EQ(av_lc_reflect(vs_words, std::size(vs_words), AV_LC_SC_OUTPUT, varyings, 32u, &found), int(AV_LC_OK));
    for (std::uint32_t adjacency = 0u; adjacency < 2u; adjacency++) {
        Words gs;
        CHECK_EQ(av_lc_build_geometry_shader(varyings, found, adjacency, 4u, 8u, &gs.data, &gs.count), int(AV_LC_OK));
        CHECK(gs.count > 5u);
        CHECK(valid(gs));
    }
}

TEST(line_coverage_spirv, a_stage_without_a_position_or_writing_the_layer_gets_no_geometry_stage)
{
    av_lc_varying varyings[32] = {};
    std::uint32_t found = 0u;
    CHECK_EQ(av_lc_reflect(layer_words, std::size(layer_words), AV_LC_SC_OUTPUT, varyings, 32u, &found),
             int(AV_LC_REFUSED_BUILTIN));
    Words gs;
    const av_lc_varying colour = {AV_LC_KIND_LOCATION, 0u, 0u, AV_LC_FLOAT, 4u, 0u};
    CHECK_EQ(av_lc_build_geometry_shader(&colour, 1u, 0u, 1u, 0u, &gs.data, &gs.count), int(AV_LC_NO_POSITION));
    CHECK(gs.data == nullptr);
    CHECK_EQ(av_lc_reflect(vs_words, 4u, AV_LC_SC_OUTPUT, varyings, 32u, &found), int(AV_LC_MALFORMED));
}

TEST(line_coverage_spirv, the_patched_pixel_shader_is_valid_and_reads_the_coverage_input)
{
    Words fs;
    CHECK_EQ(av_lc_patch_fragment_shader(fs_words, std::size(fs_words), 7u, &fs.data, &fs.count), int(AV_LC_OK));
    CHECK(valid(fs));
    CHECK_EQ(inputs_of(fs.data, fs.count), inputs_of(fs_words, std::size(fs_words)) + 1u);
    av_lc_varying varyings[32] = {};
    std::uint32_t found = 0u;
    CHECK_EQ(av_lc_reflect(fs.data, fs.count, AV_LC_SC_INPUT, varyings, 32u, &found), int(AV_LC_OK));
    CHECK_EQ(av_lc_next_location(varyings, found), 8u);
}

TEST(line_coverage_spirv, a_pixel_shader_without_a_float4_target_0_is_left_unchanged)
{
    Words fs;
    CHECK_EQ(av_lc_patch_fragment_shader(novec_words, std::size(novec_words), 1u, &fs.data, &fs.count),
             int(AV_LC_UNCHANGED));
    CHECK(fs.data == nullptr);
}
