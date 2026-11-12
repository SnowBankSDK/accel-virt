#include "av_stream_swap.h"
#include "executor/vulkan/spirv_validator.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>

namespace {

constexpr std::uint32_t streams_words[] = {
    0x07230203u, 0x00010300u, 0x0008000bu, 0x00000020u, 0x00000000u, 0x00020011u, 0x00000002u, 0x00020011u,
    0x00000035u, 0x00020011u, 0x00000036u, 0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu,
    0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0009000fu, 0x00000003u, 0x00000004u, 0x6e69616du,
    0x00000000u, 0x00000009u, 0x00000010u, 0x00000018u, 0x0000001cu, 0x00030010u, 0x00000004u, 0x0000000bu,
    0x00030010u, 0x00000004u, 0x00000013u, 0x00040010u, 0x00000004u, 0x00000000u, 0x00000001u, 0x00030010u,
    0x00000004u, 0x0000001bu, 0x00040010u, 0x00000004u, 0x0000001au, 0x00000002u, 0x00040047u, 0x00000009u,
    0x0000001du, 0x00000000u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00040047u, 0x00000009u,
    0x00000023u, 0x00000000u, 0x00040047u, 0x00000009u, 0x00000024u, 0x00000000u, 0x00040047u, 0x00000009u,
    0x00000025u, 0x00000010u, 0x00030047u, 0x0000000du, 0x00000002u, 0x00050048u, 0x0000000du, 0x00000000u,
    0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000du, 0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u,
    0x0000000du, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u, 0x0000000du, 0x00000003u, 0x0000000bu,
    0x00000004u, 0x00030047u, 0x00000016u, 0x00000002u, 0x00050048u, 0x00000016u, 0x00000000u, 0x0000000bu,
    0x00000000u, 0x00040047u, 0x00000018u, 0x0000001du, 0x00000002u, 0x00040047u, 0x00000018u, 0x00000024u,
    0x00000000u, 0x00040047u, 0x00000018u, 0x00000025u, 0x00000010u, 0x00040047u, 0x0000001cu, 0x0000001du,
    0x00000002u, 0x00040047u, 0x0000001cu, 0x0000001eu, 0x00000001u, 0x00040047u, 0x0000001cu, 0x00000024u,
    0x00000000u, 0x00040047u, 0x0000001cu, 0x00000025u, 0x00000010u, 0x00020013u, 0x00000002u, 0x00030021u,
    0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
    0x00000004u, 0x00040020u, 0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u,
    0x00000003u, 0x00040015u, 0x0000000au, 0x00000020u, 0x00000000u, 0x0004002bu, 0x0000000au, 0x0000000bu,
    0x00000001u, 0x0004001cu, 0x0000000cu, 0x00000006u, 0x0000000bu, 0x0006001eu, 0x0000000du, 0x00000007u,
    0x00000006u, 0x0000000cu, 0x0000000cu, 0x0004001cu, 0x0000000eu, 0x0000000du, 0x0000000bu, 0x00040020u,
    0x0000000fu, 0x00000001u, 0x0000000eu, 0x0004003bu, 0x0000000fu, 0x00000010u, 0x00000001u, 0x00040015u,
    0x00000011u, 0x00000020u, 0x00000001u, 0x0004002bu, 0x00000011u, 0x00000012u, 0x00000000u, 0x00040020u,
    0x00000013u, 0x00000001u, 0x00000007u, 0x0003001eu, 0x00000016u, 0x00000007u, 0x00040020u, 0x00000017u,
    0x00000003u, 0x00000016u, 0x0004003bu, 0x00000017u, 0x00000018u, 0x00000003u, 0x0004003bu, 0x00000008u,
    0x0000001cu, 0x00000003u, 0x0004002bu, 0x00000006u, 0x0000001du, 0x3f800000u, 0x0007002cu, 0x00000007u,
    0x0000001eu, 0x0000001du, 0x0000001du, 0x0000001du, 0x0000001du, 0x0004002bu, 0x00000011u, 0x0000001fu,
    0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u,
    0x00060041u, 0x00000013u, 0x00000014u, 0x00000010u, 0x00000012u, 0x00000012u, 0x0004003du, 0x00000007u,
    0x00000015u, 0x00000014u, 0x0003003eu, 0x00000009u, 0x00000015u, 0x000200dcu, 0x00000012u, 0x00060041u,
    0x00000013u, 0x00000019u, 0x00000010u, 0x00000012u, 0x00000012u, 0x0004003du, 0x00000007u, 0x0000001au,
    0x00000019u, 0x00050041u, 0x00000008u, 0x0000001bu, 0x00000018u, 0x00000012u, 0x0003003eu, 0x0000001bu,
    0x0000001au, 0x0003003eu, 0x0000001cu, 0x0000001eu, 0x000200dcu, 0x0000001fu, 0x000100fdu, 0x00010038u,
};

constexpr std::uint32_t single_words[] = {
    0x07230203u, 0x00010300u, 0x0008000bu, 0x0000001cu, 0x00000000u, 0x00020011u, 0x00000002u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0008000fu, 0x00000003u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00000013u, 0x00000019u,
    0x00030010u, 0x00000004u, 0x00000013u, 0x00040010u, 0x00000004u, 0x00000000u, 0x00000001u, 0x00030010u,
    0x00000004u, 0x0000001bu, 0x00040010u, 0x00000004u, 0x0000001au, 0x00000001u, 0x00030047u, 0x0000000bu,
    0x00000002u, 0x00050048u, 0x0000000bu, 0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu,
    0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u, 0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u,
    0x00050048u, 0x0000000bu, 0x00000003u, 0x0000000bu, 0x00000004u, 0x00030047u, 0x00000010u, 0x00000002u,
    0x00050048u, 0x00000010u, 0x00000000u, 0x0000000bu, 0x00000000u, 0x00050048u, 0x00000010u, 0x00000001u,
    0x0000000bu, 0x00000001u, 0x00050048u, 0x00000010u, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u,
    0x00000010u, 0x00000003u, 0x0000000bu, 0x00000004u, 0x00040047u, 0x00000019u, 0x0000001eu, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u,
    0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u,
    0x0004002bu, 0x00000008u, 0x00000009u, 0x00000001u, 0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u,
    0x0006001eu, 0x0000000bu, 0x00000007u, 0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu,
    0x00000003u, 0x0000000bu, 0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u, 0x0000000eu,
    0x00000020u, 0x00000001u, 0x0004002bu, 0x0000000eu, 0x0000000fu, 0x00000000u, 0x0006001eu, 0x00000010u,
    0x00000007u, 0x00000006u, 0x0000000au, 0x0000000au, 0x0004001cu, 0x00000011u, 0x00000010u, 0x00000009u,
    0x00040020u, 0x00000012u, 0x00000001u, 0x00000011u, 0x0004003bu, 0x00000012u, 0x00000013u, 0x00000001u,
    0x00040020u, 0x00000014u, 0x00000001u, 0x00000007u, 0x00040020u, 0x00000017u, 0x00000003u, 0x00000007u,
    0x0004003bu, 0x00000017u, 0x00000019u, 0x00000003u, 0x0004002bu, 0x00000006u, 0x0000001au, 0x3f800000u,
    0x0007002cu, 0x00000007u, 0x0000001bu, 0x0000001au, 0x0000001au, 0x0000001au, 0x0000001au, 0x00050036u,
    0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x00060041u, 0x00000014u,
    0x00000015u, 0x00000013u, 0x0000000fu, 0x0000000fu, 0x0004003du, 0x00000007u, 0x00000016u, 0x00000015u,
    0x00050041u, 0x00000017u, 0x00000018u, 0x0000000du, 0x0000000fu, 0x0003003eu, 0x00000018u, 0x00000016u,
    0x0003003eu, 0x00000019u, 0x0000001bu, 0x000100dau, 0x000100dbu, 0x000100fdu, 0x00010038u,
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

struct Streams {
    std::uint32_t emitted[4] = {};
    std::uint32_t outputs[4] = {};
    std::uint32_t undecorated = 0u;
    bool          capability = false;
};

Streams streams_of(const std::uint32_t *words, std::size_t count)
{
    Streams found;
    std::uint32_t constants[256] = {};
    bool          decorated[256] = {};
    std::uint32_t outputs[64] = {};
    std::uint32_t output_count = 0u;
    for (std::size_t at = 5u; at < count; at += words[at] >> 16u) {
        const std::uint32_t opcode = words[at] & 0xffffu;
        if (opcode == 17u && words[at + 1u] == 54u) found.capability = true;
        if (opcode == 43u && words[at + 2u] < 256u) constants[words[at + 2u]] = words[at + 3u];
        if (opcode == 59u && words[at + 3u] == 3u && output_count < 64u) outputs[output_count++] = words[at + 2u];
        if (opcode == 71u && words[at + 2u] == 29u && words[at + 1u] < 256u) {
            decorated[words[at + 1u]] = true;
            found.outputs[words[at + 3u] & 3u]++;
        }
        if (opcode == 220u) found.emitted[constants[words[at + 1u]] & 3u]++;
    }
    for (std::uint32_t index = 0u; index < output_count; index++)
        if (!decorated[outputs[index]]) found.undecorated++;
    return found;
}

}

TEST(stream_swap_spirv, the_rasterized_stream_and_stream_zero_trade_their_outputs_and_their_vertices)
{
    const Streams before = streams_of(streams_words, std::size(streams_words));
    CHECK_EQ(before.outputs[0], 1u);
    CHECK_EQ(before.outputs[2], 2u);
    Words swapped;
    CHECK_EQ(av_ss_rasterize_stream_as_zero(streams_words, std::size(streams_words), 2u, &swapped.data, &swapped.count),
             int(AV_SS_OK));
    CHECK(valid(swapped));
    const Streams after = streams_of(swapped.data, swapped.count);
    CHECK_EQ(after.outputs[0], 2u);
    CHECK_EQ(after.outputs[2], 1u);
    CHECK_EQ(after.emitted[0], before.emitted[2]);
    CHECK_EQ(after.emitted[2], before.emitted[0]);
    CHECK_EQ(after.undecorated, 0u);
}

TEST(stream_swap_spirv, a_single_stream_shader_rasterized_from_another_stream_emits_and_decorates_that_stream)
{
    const Streams before = streams_of(single_words, std::size(single_words));
    CHECK(!before.capability);
    CHECK_EQ(before.undecorated, 2u);
    Words swapped;
    CHECK_EQ(av_ss_rasterize_stream_as_zero(single_words, std::size(single_words), 1u, &swapped.data, &swapped.count),
             int(AV_SS_OK));
    CHECK(valid(swapped));
    const Streams after = streams_of(swapped.data, swapped.count);
    CHECK(after.capability);
    CHECK_EQ(after.undecorated, 0u);
    CHECK_EQ(after.outputs[1], 2u);
    CHECK_EQ(after.emitted[1], 1u);
    CHECK_EQ(after.emitted[0], 0u);
}

TEST(stream_swap_spirv, stream_zero_and_malformed_modules_are_left_unchanged)
{
    Words swapped;
    CHECK_EQ(av_ss_rasterize_stream_as_zero(streams_words, std::size(streams_words), 0u, &swapped.data, &swapped.count),
             int(AV_SS_UNCHANGED));
    CHECK(swapped.data == nullptr);
    CHECK_EQ(av_ss_rasterize_stream_as_zero(streams_words, 4u, 2u, &swapped.data, &swapped.count), int(AV_SS_MALFORMED));
    CHECK(swapped.data == nullptr);
}
