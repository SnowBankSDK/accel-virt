#include "av_forced_coverage.h"
#include "executor/vulkan/spirv_validator.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>

namespace {

constexpr std::uint32_t mask_words[] = {
    0x07230203u, 0x00010000u, 0x0008000bu, 0x0000002du, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0007000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00000015u, 0x00030010u,
    0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du,
    0x00000000u, 0x00030005u, 0x00000008u, 0x00000063u, 0x00060005u, 0x0000000du, 0x535f6c67u, 0x6c706d61u,
    0x73614d65u, 0x006e496bu, 0x00060005u, 0x00000015u, 0x465f6c67u, 0x43676172u, 0x64726f6fu, 0x00000000u,
    0x00030005u, 0x00000021u, 0x00766f63u, 0x00040047u, 0x0000000du, 0x0000000bu, 0x00000014u, 0x00030047u,
    0x0000000du, 0x0000000eu, 0x00040047u, 0x00000015u, 0x0000000bu, 0x0000000fu, 0x00040047u, 0x00000021u,
    0x00000021u, 0x00000000u, 0x00040047u, 0x00000021u, 0x00000022u, 0x00000000u, 0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u, 0x00000001u, 0x00040020u,
    0x00000007u, 0x00000007u, 0x00000006u, 0x00040015u, 0x00000009u, 0x00000020u, 0x00000000u, 0x0004002bu,
    0x00000009u, 0x0000000au, 0x00000001u, 0x0004001cu, 0x0000000bu, 0x00000006u, 0x0000000au, 0x00040020u,
    0x0000000cu, 0x00000001u, 0x0000000bu, 0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000001u, 0x0004002bu,
    0x00000006u, 0x0000000eu, 0x00000000u, 0x00040020u, 0x0000000fu, 0x00000001u, 0x00000006u, 0x00030016u,
    0x00000012u, 0x00000020u, 0x00040017u, 0x00000013u, 0x00000012u, 0x00000004u, 0x00040020u, 0x00000014u,
    0x00000001u, 0x00000013u, 0x0004003bu, 0x00000014u, 0x00000015u, 0x00000001u, 0x0004002bu, 0x00000009u,
    0x00000016u, 0x00000000u, 0x00040020u, 0x00000017u, 0x00000001u, 0x00000012u, 0x0004002bu, 0x00000012u,
    0x0000001au, 0x40400000u, 0x00020014u, 0x0000001bu, 0x00090019u, 0x0000001fu, 0x00000009u, 0x00000001u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000002u, 0x00000021u, 0x00040020u, 0x00000020u, 0x00000000u,
    0x0000001fu, 0x0004003bu, 0x00000020u, 0x00000021u, 0x00000000u, 0x00040017u, 0x00000022u, 0x00000012u,
    0x00000002u, 0x00040017u, 0x00000025u, 0x00000006u, 0x00000002u, 0x00040020u, 0x0000002au, 0x0000000bu,
    0x00000009u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
    0x0004003bu, 0x00000007u, 0x00000008u, 0x00000007u, 0x00050041u, 0x0000000fu, 0x00000010u, 0x0000000du,
    0x0000000eu, 0x0004003du, 0x00000006u, 0x00000011u, 0x00000010u, 0x0003003eu, 0x00000008u, 0x00000011u,
    0x00050041u, 0x00000017u, 0x00000018u, 0x00000015u, 0x00000016u, 0x0004003du, 0x00000012u, 0x00000019u,
    0x00000018u, 0x000500bau, 0x0000001bu, 0x0000001cu, 0x00000019u, 0x0000001au, 0x000300f7u, 0x0000001eu,
    0x00000000u, 0x000400fau, 0x0000001cu, 0x0000001du, 0x0000001eu, 0x000200f8u, 0x0000001du, 0x0004003du,
    0x00000013u, 0x00000023u, 0x00000015u, 0x0007004fu, 0x00000022u, 0x00000024u, 0x00000023u, 0x00000023u,
    0x00000000u, 0x00000001u, 0x0004006eu, 0x00000025u, 0x00000026u, 0x00000024u, 0x0004003du, 0x00000006u,
    0x00000027u, 0x00000008u, 0x000400cdu, 0x00000006u, 0x00000028u, 0x00000027u, 0x0004007cu, 0x00000009u,
    0x00000029u, 0x00000028u, 0x0006003cu, 0x0000002au, 0x0000002bu, 0x00000021u, 0x00000026u, 0x00000016u,
    0x000700eau, 0x00000009u, 0x0000002cu, 0x0000002bu, 0x0000000au, 0x00000016u, 0x00000029u, 0x000200f9u,
    0x0000001eu, 0x000200f8u, 0x0000001eu, 0x000100fdu, 0x00010038u,
};

constexpr std::uint32_t plain_words[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000032u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0008000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000bu, 0x00000025u, 0x00000027u,
    0x00030010u, 0x00000004u, 0x00000007u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u,
    0x6e69616du, 0x00000000u, 0x00030005u, 0x00000009u, 0x00000076u, 0x00040005u, 0x0000000bu, 0x6f6c6f63u,
    0x00000072u, 0x00030005u, 0x0000000fu, 0x00000069u, 0x00030005u, 0x00000025u, 0x00766f63u, 0x00060005u,
    0x00000027u, 0x465f6c67u, 0x43676172u, 0x64726f6fu, 0x00000000u, 0x00040047u, 0x0000000bu, 0x0000001eu,
    0x00000000u, 0x00040047u, 0x00000025u, 0x00000021u, 0x00000000u, 0x00040047u, 0x00000025u, 0x00000022u,
    0x00000000u, 0x00040047u, 0x00000027u, 0x0000000bu, 0x0000000fu, 0x00020013u, 0x00000002u, 0x00030021u,
    0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
    0x00000004u, 0x00040020u, 0x00000008u, 0x00000007u, 0x00000007u, 0x00040020u, 0x0000000au, 0x00000001u,
    0x00000007u, 0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000001u, 0x00040015u, 0x0000000du, 0x00000020u,
    0x00000001u, 0x00040020u, 0x0000000eu, 0x00000007u, 0x0000000du, 0x0004002bu, 0x0000000du, 0x00000010u,
    0x00000000u, 0x0004002bu, 0x0000000du, 0x00000017u, 0x00000003u, 0x00020014u, 0x00000018u, 0x0004002bu,
    0x00000006u, 0x0000001bu, 0x3f000000u, 0x0004002bu, 0x0000000du, 0x00000020u, 0x00000001u, 0x00040015u,
    0x00000022u, 0x00000020u, 0x00000000u, 0x00090019u, 0x00000023u, 0x00000022u, 0x00000001u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000002u, 0x00000021u, 0x00040020u, 0x00000024u, 0x00000000u, 0x00000023u,
    0x0004003bu, 0x00000024u, 0x00000025u, 0x00000000u, 0x0004003bu, 0x0000000au, 0x00000027u, 0x00000001u,
    0x00040017u, 0x00000028u, 0x00000006u, 0x00000002u, 0x00040017u, 0x0000002bu, 0x0000000du, 0x00000002u,
    0x0004002bu, 0x00000006u, 0x0000002eu, 0x437f0000u, 0x00040017u, 0x00000030u, 0x00000022u, 0x00000004u,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003bu,
    0x00000008u, 0x00000009u, 0x00000007u, 0x0004003bu, 0x0000000eu, 0x0000000fu, 0x00000007u, 0x0004003du,
    0x00000007u, 0x0000000cu, 0x0000000bu, 0x0003003eu, 0x00000009u, 0x0000000cu, 0x0003003eu, 0x0000000fu,
    0x00000010u, 0x000200f9u, 0x00000011u, 0x000200f8u, 0x00000011u, 0x000400f6u, 0x00000013u, 0x00000014u,
    0x00000000u, 0x000200f9u, 0x00000015u, 0x000200f8u, 0x00000015u, 0x0004003du, 0x0000000du, 0x00000016u,
    0x0000000fu, 0x000500b1u, 0x00000018u, 0x00000019u, 0x00000016u, 0x00000017u, 0x000400fau, 0x00000019u,
    0x00000012u, 0x00000013u, 0x000200f8u, 0x00000012u, 0x0004003du, 0x00000007u, 0x0000001au, 0x00000009u,
    0x0005008eu, 0x00000007u, 0x0000001cu, 0x0000001au, 0x0000001bu, 0x0004003du, 0x00000007u, 0x0000001du,
    0x00000009u, 0x00050081u, 0x00000007u, 0x0000001eu, 0x0000001du, 0x0000001cu, 0x0003003eu, 0x00000009u,
    0x0000001eu, 0x000200f9u, 0x00000014u, 0x000200f8u, 0x00000014u, 0x0004003du, 0x0000000du, 0x0000001fu,
    0x0000000fu, 0x00050080u, 0x0000000du, 0x00000021u, 0x0000001fu, 0x00000020u, 0x0003003eu, 0x0000000fu,
    0x00000021u, 0x000200f9u, 0x00000011u, 0x000200f8u, 0x00000013u, 0x0004003du, 0x00000023u, 0x00000026u,
    0x00000025u, 0x0004003du, 0x00000007u, 0x00000029u, 0x00000027u, 0x0007004fu, 0x00000028u, 0x0000002au,
    0x00000029u, 0x00000029u, 0x00000000u, 0x00000001u, 0x0004006eu, 0x0000002bu, 0x0000002cu, 0x0000002au,
    0x0004003du, 0x00000007u, 0x0000002du, 0x00000009u, 0x0005008eu, 0x00000007u, 0x0000002fu, 0x0000002du,
    0x0000002eu, 0x0004006du, 0x00000030u, 0x00000031u, 0x0000002fu, 0x00050063u, 0x00000026u, 0x0000002cu,
    0x00000031u, 0x00002000u, 0x000100fdu, 0x00010038u,
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

struct Shape {
    bool          barycentric_capability = false;
    std::uint32_t barycentric_inputs = 0u;
    std::uint32_t sample_masks = 0u;
    std::uint32_t terminators = 0u;
    std::uint32_t coverage_bits = 0u;
    std::uint32_t mask_reads_of_input = 0u;
    std::uint32_t interface = 0u;
};

Shape shape_of(const std::uint32_t *words, std::size_t count)
{
    Shape found;
    std::uint32_t mask_input = 0u, uint_type = 0u;
    for (std::size_t at = 5u; at < count; at += words[at] >> 16u) {
        const std::uint32_t opcode = words[at] & 0xffffu;
        if (opcode == 17u && words[at + 1u] == 5284u) found.barycentric_capability = true;
        if (opcode == 71u && words[at + 2u] == 11u && words[at + 3u] == 5287u) found.barycentric_inputs++;
        if (opcode == 71u && words[at + 2u] == 11u && words[at + 3u] == 20u) {
            found.sample_masks++;
            mask_input = words[at + 1u];
        }
        if (opcode == 15u) found.interface = (words[at] >> 16u) - 3u;
        if (opcode == 21u && words[at + 2u] == 32u && words[at + 3u] == 0u) uint_type = words[at + 1u];
        if (opcode == 43u && words[at + 1u] == uint_type && words[at + 3u] != 0u &&
            (words[at + 3u] & (words[at + 3u] - 1u)) == 0u)
            found.coverage_bits |= words[at + 3u];
        if (opcode == 252u || opcode == 4416u) found.terminators++;
        if ((opcode == 61u || opcode == 65u) && words[at + 3u] == mask_input) found.mask_reads_of_input++;
    }
    return found;
}

}

TEST(forced_coverage_spirv, a_pixel_shader_reading_its_coverage_reads_the_mask_it_computes_for_every_forced_sample)
{
    const Shape before = shape_of(mask_words, std::size(mask_words));
    CHECK_EQ(before.sample_masks, 1u);
    CHECK(before.mask_reads_of_input >= 1u);
    for (const std::uint32_t samples : {2u, 4u, 8u, 16u}) {
        Words patched;
        CHECK_EQ(av_fc_compute_coverage(mask_words, std::size(mask_words), samples, &patched.data, &patched.count),
                 int(AV_FC_OK));
        CHECK(valid(patched));
        const Shape after = shape_of(patched.data, patched.count);
        CHECK(after.barycentric_capability);
        CHECK_EQ(after.barycentric_inputs, 1u);
        CHECK_EQ(after.terminators, 1u);
        CHECK_EQ(after.mask_reads_of_input, 0u);
        CHECK_EQ(after.interface, before.interface + 1u);
        CHECK_EQ(after.coverage_bits & ((samples == 16u ? 0xffffu : (1u << samples) - 1u)),
                 samples == 16u ? 0xffffu : (1u << samples) - 1u);
    }
}

TEST(forced_coverage_spirv, a_pixel_shader_that_ignores_its_coverage_still_runs_only_where_a_forced_sample_is_covered)
{
    Words patched;
    CHECK_EQ(av_fc_compute_coverage(plain_words, std::size(plain_words), 16u, &patched.data, &patched.count), int(AV_FC_OK));
    CHECK(valid(patched));
    const Shape after = shape_of(patched.data, patched.count);
    CHECK(after.barycentric_capability);
    CHECK_EQ(after.barycentric_inputs, 1u);
    CHECK_EQ(after.terminators, 1u);
    CHECK_EQ(after.sample_masks, 0u);
}

TEST(forced_coverage_spirv, a_count_without_a_standard_pattern_or_a_module_that_is_not_spirv_is_refused)
{
    Words patched;
    CHECK_EQ(av_fc_compute_coverage(plain_words, std::size(plain_words), 3u, &patched.data, &patched.count),
             int(AV_FC_UNSUPPORTED));
    CHECK(patched.data == nullptr);
    const std::uint32_t garbage[] = {0x12345678u, 0u, 0u, 1u, 0u};
    CHECK_EQ(av_fc_compute_coverage(garbage, std::size(garbage), 16u, &patched.data, &patched.count),
             int(AV_FC_MALFORMED));
    CHECK(patched.data == nullptr);
}
