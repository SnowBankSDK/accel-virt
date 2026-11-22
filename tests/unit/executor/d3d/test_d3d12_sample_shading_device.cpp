#include "fixtures/d3d12/sample_shading.hpp"
#include "executor/d3d/d3d_backend.hpp"
#include "executor/host_memory_source.hpp"
#include "executor/sharing/session_shares.hpp"
#include "executor/shim/d3d11_host_device.hpp"
#include "executor/shim/d3d12_host_device.hpp"
#include "executor/shim/loader_host_apis.hpp"
#include "harness/executor/d3d12_answers.hpp"
#include "harness/executor/d3d_loopback.hpp"
#include "harness/executor/executor_log_rig.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/generated/d3d12/d3d12_dispatch_client.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <vector>

extern "C" const char *__lsan_default_suppressions(void);
extern "C" const char *__lsan_default_suppressions(void)
{
    return "leak:libvulkan\n"
           "leak:libdxvk\n"
           "leak:libvulkan_radeon\n"
           "leak:libvkd3d\n"
           "leak:libz\n";
}

namespace {

using namespace av::d3d12proto;

constexpr std::uint32_t format_r32g32_float = 16u;
constexpr std::uint32_t topology_type_triangle = 3u;
constexpr std::size_t   region_bytes = 8u << 20u;

constexpr std::uint32_t spirv_magic = 0x07230203u;
constexpr std::uint32_t op_ext_inst = 12u, op_entry_point = 15u, op_variable = 59u, op_access_chain = 65u, op_decorate = 71u;
constexpr std::uint32_t interpolate_at_centroid = 76u, interpolate_at_sample = 77u, interpolate_at_offset = 78u;
constexpr std::uint32_t model_fragment = 4u, storage_input = 1u;
constexpr std::uint32_t decoration_builtin = 11u, decoration_no_perspective = 13u, decoration_flat = 14u,
                        decoration_centroid = 16u, decoration_sample = 17u, decoration_location = 30u;
constexpr std::uint32_t builtin_sample_id = 18u;

enum Id : std::uint32_t { root = 1, per_sample_pipeline, per_pixel_pipeline };

struct Input {
    std::uint32_t id = 0u;
    bool          located = false;
    bool          flat = false;
    bool          no_perspective = false;
    bool          centroid = false;
    bool          sample = false;
    bool          sample_id = false;
    std::uint32_t at_centre = 0u;
    std::uint32_t at_centroid = 0u;
    std::uint32_t at_sample = 0u;
};

struct Chain {
    std::uint32_t id;
    std::uint32_t base;
};

struct Fragment {
    std::vector<Input> inputs;

    bool reads_sample_id() const
    {
        for (const Input &input : inputs)
            if (input.sample_id) return true;
        return false;
    }
};

Input &input_of(std::vector<Input> &inputs, std::uint32_t id)
{
    for (Input &input : inputs)
        if (input.id == id) return input;
    inputs.push_back(Input{});
    inputs.back().id = id;
    return inputs.back();
}

bool read_fragment(const char *path, Fragment &fragment)
{
    std::FILE *const file = std::fopen(path, "rb");
    if (file == nullptr) return false;
    std::vector<std::uint32_t> words(1u << 16u);
    const std::size_t          count = std::fread(words.data(), sizeof(std::uint32_t), words.size(), file);
    std::fclose(file);
    if (count < 5u || words[0] != spirv_magic) return false;
    bool               fragment_stage = false;
    std::vector<Input> decorated;
    std::vector<std::uint32_t> stage_inputs;
    std::vector<Chain>         chains;
    for (std::size_t at = 5u; at < count;) {
        const std::uint32_t length = words[at] >> 16u;
        const std::uint32_t op = words[at] & 0xffffu;
        if (length == 0u || at + length > count) return false;
        if (op == op_entry_point && length > 1u) fragment_stage = fragment_stage || words[at + 1u] == model_fragment;
        if (op == op_variable && length > 3u && words[at + 3u] == storage_input) stage_inputs.push_back(words[at + 2u]);
        if (op == op_decorate && length > 2u) {
            Input              &input = input_of(decorated, words[at + 1u]);
            const std::uint32_t decoration = words[at + 2u];
            input.located = input.located || decoration == decoration_location;
            input.flat = input.flat || decoration == decoration_flat;
            input.no_perspective = input.no_perspective || decoration == decoration_no_perspective;
            input.centroid = input.centroid || decoration == decoration_centroid;
            input.sample = input.sample || decoration == decoration_sample;
            input.sample_id = input.sample_id ||
                              (decoration == decoration_builtin && length > 3u && words[at + 3u] == builtin_sample_id);
        }
        if (op == op_access_chain && length > 3u) chains.push_back(Chain{words[at + 2u], words[at + 3u]});
        if (op == op_ext_inst && length > 5u) {
            std::uint32_t interpolant = words[at + 5u];
            for (const Chain &chain : chains)
                if (chain.id == interpolant) interpolant = chain.base;
            Input &input = input_of(decorated, interpolant);
            input.at_centroid += words[at + 4u] == interpolate_at_centroid ? 1u : 0u;
            input.at_sample += words[at + 4u] == interpolate_at_sample ? 1u : 0u;
            input.at_centre += words[at + 4u] == interpolate_at_offset ? 1u : 0u;
        }
        at += length;
    }
    if (!fragment_stage) return false;
    for (const std::uint32_t id : stage_inputs) fragment.inputs.push_back(input_of(decorated, id));
    return true;
}

std::vector<Fragment> dumped_fragments(const char *directory)
{
    std::vector<Fragment> fragments;
    DIR *const            listing = opendir(directory);
    if (listing == nullptr) return fragments;
    for (const dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
        char path[512] = {};
        std::snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        const std::size_t length = std::strlen(entry->d_name);
        Fragment          fragment;
        if (length > 4u && std::strcmp(entry->d_name + length - 4u, ".spv") == 0 && read_fragment(path, fragment))
            fragments.push_back(fragment);
        if (entry->d_name[0] != '.') unlink(path);
    }
    closedir(listing);
    rmdir(directory);
    return fragments;
}

bool host_devices(av::LoaderHostApis &apis, av::D3d11HostDevice *&device11, av::D3d12HostDevice *&device12,
                  av::HostMemorySource *&source)
{
    if (std::getenv("AV_D3D_PATH") == nullptr) setenv("AV_D3D_PATH", AV_FIXTURE_DIR "/../../build-d3d/lib", 1);
    apis.open();
    const char *const wanted = std::getenv("AV_TEST_HOST_GPU");
    for (std::uint32_t index = 0u; index < apis.gpu_count(); index++) {
        if (wanted != nullptr) {
            std::uint8_t candidate[16] = {};
            apis.gpu_uuid(index, candidate);
            char text[33] = {};
            for (std::uint32_t at = 0u; at < 16u; at++) std::snprintf(text + 2u * at, 3u, "%02x", candidate[at]);
            if (std::strcmp(text, wanted) != 0) continue;
        }
        device11 = apis.d3d11_device(index);
        device12 = apis.d3d12_device(index);
        source = apis.memory_source(index);
        if (device11 != nullptr && device12 != nullptr && source != nullptr) return true;
    }
    std::fprintf(stderr, "no GPU here runs vkd3d-proton: nothing judged\n");
    return false;
}

bool make_pipeline(av::D3dLoopback &wire, std::uint32_t id, const std::uint32_t *pixel, std::uint64_t pixel_bytes)
{
    GraphicsState state = {};
    for (RenderTargetBlend &blend : state.targets) {
        blend.src_blend = blend.src_blend_alpha = 2u;
        blend.dest_blend = blend.dest_blend_alpha = 1u;
        blend.blend_op = blend.blend_op_alpha = 1u;
        blend.logic_op = 4u;
        blend.write_mask = 0xfu;
    }
    state.sample_mask = 0xffffffffu;
    state.rasterizer.fill_mode = 3u;
    state.rasterizer.cull_mode = 1u;
    state.rasterizer.depth_clip = 1u;
    state.depth_stencil.depth_func = 8u;
    state.topology_type = topology_type_triangle;
    state.sample_count = 1u;
    const InputElement position = {0u, 0u, format_r32g32_float, 0u, 0u, 0u};
    return av_d3d12_CreateGraphicsPipeline12(wire, id, root, state, 1u, &position, 0u, nullptr, 0u, nullptr, 0u,
                                             nullptr, 0xFFFFFFFFu, sample_shading::vertex,
                                             sample_shading::vertex_bytes, nullptr, 0u, nullptr, 0u, nullptr, 0u, pixel,
                                             pixel_bytes, nullptr, 0u, nullptr, 0u, nullptr, 0u);
}

}

TEST(d3d12_sample_shading_device, a_pixel_shader_that_reads_the_sample_index_takes_its_plain_inputs_at_the_pixel_centre_and_its_centroid_inputs_at_the_centroid_and_one_that_runs_per_pixel_loads_them_as_they_are)
{
    char directory[] = "/tmp/av-sample-shading-XXXXXX";
    CHECK(mkdtemp(directory) != nullptr);
    setenv("VKD3D_SHADER_DUMP_PATH", directory, 1);
    {
        av::ExecutorLogRig    rig_log;
        av::LoaderHostApis    apis(rig_log.log);
        av::D3d11HostDevice  *device11 = nullptr;
        av::D3d12HostDevice  *device12 = nullptr;
        av::HostMemorySource *source = nullptr;
        if (!host_devices(apis, device11, device12, source)) {
            unsetenv("VKD3D_SHADER_DUMP_PATH");
            static_cast<void>(dumped_fragments(directory));
            return;
        }
        av::D3dBackend    backend(rig_log.log);
        av::SessionShares unshared;
        CHECK(backend.initialize(unshared, rig_log.imports, device11, device12));
        {
            av::D3dLoopback wire(backend, rig_log.imports, source, region_bytes, region_bytes);
            std::uint32_t   hresult = 1u;
            CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
            CHECK_EQ(hresult, 0u);
            CHECK(av_d3d12_CreateRootSignature12(wire, root, 1u, 0u, nullptr, 0u, nullptr, 0u, nullptr));
            CHECK(make_pipeline(wire, per_sample_pipeline, sample_shading::per_sample_pixel,
                                sample_shading::per_sample_pixel_bytes));
            CHECK(make_pipeline(wire, per_pixel_pipeline, sample_shading::per_pixel_pixel,
                                sample_shading::per_pixel_pixel_bytes));
            std::uint32_t removed = 1u;
            CHECK(av_d3d12_Finish12(wire, &removed));
            CHECK_EQ(removed, 0u);
            CHECK_EQ(wire.refused(), 0u);
        }
        backend.shut_down();
    }
    unsetenv("VKD3D_SHADER_DUMP_PATH");
    const std::vector<Fragment> fragments = dumped_fragments(directory);
    std::uint32_t per_sample = 0u, per_pixel = 0u;
    for (const Fragment &fragment : fragments) {
        const bool    sampled = fragment.reads_sample_id();
        std::uint32_t plain = 0u, flat = 0u, kept_plane = 0u, centroid = 0u;
        for (const Input &input : fragment.inputs) {
            if (!input.located) continue;
            CHECK(!input.sample);
            CHECK_EQ(input.at_sample, 0u);
            if (input.flat) {
                flat++;
                CHECK_EQ(input.at_centre + input.at_centroid, 0u);
                continue;
            }
            kept_plane += input.no_perspective ? 1u : 0u;
            if (input.centroid) {
                centroid++;
                CHECK_EQ(input.at_centre, 0u);
                CHECK_EQ(input.at_centroid != 0u, sampled);
                continue;
            }
            plain++;
            CHECK_EQ(input.at_centroid, 0u);
            CHECK_EQ(input.at_centre != 0u, sampled);
        }
        CHECK_EQ(plain, 2u);
        CHECK_EQ(centroid, 1u);
        CHECK_EQ(flat, 1u);
        CHECK_EQ(kept_plane, 1u);
        per_sample += sampled ? 1u : 0u;
        per_pixel += sampled ? 0u : 1u;
    }
    CHECK_EQ(per_sample, 1u);
    CHECK_EQ(per_pixel, 1u);
}
