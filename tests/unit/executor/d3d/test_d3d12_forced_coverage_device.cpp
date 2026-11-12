#include "fixtures/d3d12/coverage_counting.hpp"
#include "executor/d3d/d3d_backend.hpp"
#include "executor/host_memory_source.hpp"
#include "executor/owned_fd.hpp"
#include "executor/sharing/session_shares.hpp"
#include "executor/shim/d3d11_host_device.hpp"
#include "executor/shim/d3d12_host_device.hpp"
#include "executor/shim/loader_host_apis.hpp"
#include "executor/source_memory.hpp"
#include "harness/executor/d3d12_answers.hpp"
#include "harness/executor/d3d_loopback.hpp"
#include "harness/executor/executor_log_rig.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/generated/d3d12/d3d12_dispatch_client.h"
#include "proto/resources/host_memory_request.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using namespace av::d3d12proto;

constexpr std::uint32_t size = 64u;
constexpr std::uint32_t row_pitch = size * 4u;
constexpr std::uint32_t format_r32_uint = 42u, format_r32g32_float = 16u;
constexpr std::uint32_t heap_default = 1u, heap_upload = 2u, heap_readback = 3u;
constexpr std::uint32_t state_unordered = 0x8u, state_copy_dest = 0x400u, state_copy_source = 0x800u,
                        state_generic_read = 0xac3u;
constexpr std::uint32_t flag_unordered = 0x4u;
constexpr std::uint32_t descriptors_view = 0u;
constexpr std::uint32_t heap_shader_visible = 0x1u;
constexpr std::uint32_t dimension_buffer = 1u, dimension_texture2d = 3u;
constexpr std::uint32_t layout_row_major = 1u;
constexpr std::uint32_t uav_texture2d = 4u;
constexpr std::uint32_t parameter_table = 0u, range_uav = 1u, visibility_all = 0u;
constexpr std::uint32_t topology_triangle_list = 4u, topology_type_triangle = 3u;
constexpr std::uint32_t triangles = 24u;

constexpr std::size_t   region_bytes = 8u << 20u;
constexpr std::uint64_t heap_bytes = 64u * 1024u;
constexpr std::uint64_t readback_address = 0x100000000ull;
constexpr std::uint64_t upload_address = 0x200000000ull;

enum Id : std::uint32_t {
    queue = 1, allocator, list, readback_heap, readback, upload_heap, vertices, coverage, view_heap, cpu_heap, root,
    first_pipeline
};

struct Sample {
    int x, y;
};

constexpr Sample pattern4[4] = {{-2, -6}, {6, -2}, {-6, 2}, {2, 6}};
constexpr Sample pattern8[8] = {{1, -3}, {-1, 3}, {5, 1}, {-3, -5}, {-5, 5}, {-7, -1}, {3, 7}, {7, -7}};
constexpr Sample pattern16[16] = {{1, 1},   {-1, -3}, {-3, 2}, {4, -1}, {-5, -2}, {2, 5},  {5, 3}, {3, -5},
                                  {-2, 6},  {0, -7},  {-4, -6}, {-6, 4}, {-8, 0}, {7, -4}, {6, 7}, {-7, -8}};

const Sample *pattern(std::uint32_t samples)
{
    return samples == 4u ? pattern4 : samples == 8u ? pattern8 : pattern16;
}

struct Rig {
    std::uint64_t readback_memory = 0u;
    std::uint64_t upload_memory = 0u;
    float         positions[triangles * 6u] = {};
};

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

bool imports(av::D3d12HostDevice &device, av::D3dLoopback &wire)
{
    const av::HostMemoryRequest   memory_kind(av::HostMemoryRequest::Kind::readback, 0u);
    const std::uint64_t           memory = wire.open_memory(memory_kind, heap_bytes);
    const av::SourceMemory *const held = wire.source_memory(memory);
    av::OwnedFd                   shared;
    const bool                    exported = held != nullptr && held->share(shared);
    ID3D12Heap *const imported =
        exported ? device.import_dmabuf_heap(held->pointer(), shared.fd(), held->size()) : nullptr;
    wire.close_memory(memory);
    if (imported == nullptr) {
        std::fprintf(stderr, "this vkd3d-proton opens no heap over a dma-buf (tools/vkd3d-proton-patches): nothing judged\n");
        return false;
    }
    imported->Release();
    return true;
}

HeapProperties heap_of(std::uint32_t type) { return HeapProperties{type, 0u, 0u, 0u}; }

ResourceDesc buffer_of(std::uint64_t bytes)
{
    ResourceDesc desc = {};
    desc.width = bytes;
    desc.dimension = dimension_buffer;
    desc.height = 1u;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.sample_count = 1u;
    desc.layout = layout_row_major;
    return desc;
}

void make_scene(av::D3dLoopback &wire, Rig &rig)
{
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateCommandQueue12(wire, queue, 0u, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, allocator, 0u));
    CHECK(av_d3d12_CreateCommandList12(wire, list, allocator, 0u, 0u));
    rig.readback_memory = wire.open_memory(av::HostMemoryRequest(av::HostMemoryRequest::Kind::readback, 0u), heap_bytes);
    CHECK(rig.readback_memory != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, readback_heap, heap_of(heap_readback), 0u, heap_bytes, 0u, rig.readback_memory, 0u));
    CHECK(av_d3d12_CreatePlacedResource12(wire, readback, readback_heap, 0u, buffer_of(heap_bytes), state_copy_dest,
                                           ClearValue{}, readback_address));
    rig.upload_memory = wire.open_memory(av::HostMemoryRequest(av::HostMemoryRequest::Kind::upload, 0u), heap_bytes);
    CHECK(rig.upload_memory != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, rig.upload_memory, 0u));
    std::uint32_t seed = 12345u;
    const auto    next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8u) / static_cast<float>(1u << 24u) * 2.2f - 1.1f;
    };
    for (float &value : rig.positions) value = next();
    std::memcpy(wire.memory(rig.upload_memory, 0u), rig.positions, sizeof rig.positions);
    CHECK(av_d3d12_CreatePlacedResource12(wire, vertices, upload_heap, 0u, buffer_of(sizeof rig.positions),
                                           state_generic_read, ClearValue{}, upload_address));

    ResourceDesc texture = {};
    texture.width = size;
    texture.height = size;
    texture.dimension = dimension_texture2d;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_r32_uint;
    texture.sample_count = 1u;
    texture.flags = flag_unordered;
    CHECK(av_d3d12_CreateCommittedResource12(wire, coverage, heap_of(heap_default), 0u, texture, state_unordered,
                                              ClearValue{}, 0u));
    ViewDesc view = {};
    view.resource = coverage;
    view.format = format_r32_uint;
    view.dimension = uav_texture2d;
    CHECK(av_d3d12_CreateDescriptorHeap12(wire, view_heap, descriptors_view, 1u, heap_shader_visible));
    CHECK(av_d3d12_CreateDescriptorHeap12(wire, cpu_heap, descriptors_view, 1u, 0u));
    CHECK(av_d3d12_CreateUnorderedAccessView12(wire, DescriptorRef{view_heap, 0u}, 1u, view));
    CHECK(av_d3d12_CreateUnorderedAccessView12(wire, DescriptorRef{cpu_heap, 0u}, 1u, view));
    const DescriptorRange range = {range_uav, 1u, 0u, 0u, 0u, 0u};
    const RootParameter   table = {parameter_table, visibility_all, 0u, 0u, 0u, 0u, 1u, 0u};
    CHECK(av_d3d12_CreateRootSignature12(wire, root, 1u, 1u, &table, 1u, &range, 0u, nullptr));
}

bool make_pipeline(av::D3dLoopback &wire, std::uint32_t id, std::uint32_t samples)
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
    state.rasterizer.forced_sample_count = samples;
    state.depth_stencil.depth_func = 8u;
    state.topology_type = topology_type_triangle;
    state.sample_count = 1u;
    const InputElement position = {0u, 0u, format_r32g32_float, 0u, 0u, 0u};
    return av_d3d12_CreateGraphicsPipeline12(wire, id, root, state, 1u, &position, 0u, nullptr, 0u, nullptr, 0u,
                                             nullptr, 0xFFFFFFFFu, coverage_counting::vertex,
                                             coverage_counting::vertex_bytes, nullptr, 0u, nullptr, 0u, nullptr, 0u,
                                             coverage_counting::pixel, coverage_counting::pixel_bytes, nullptr, 0u,
                                             nullptr, 0u);
}

bool draw_counts(av::D3dLoopback &wire, Rig &rig, std::uint32_t pipeline, std::vector<std::uint32_t> &counts)
{
    const std::uint32_t heaps[1] = {view_heap};
    CHECK(av_d3d12_SetDescriptorHeaps12(wire, list, 1u, heaps));
    CHECK(av_d3d12_ClearUnorderedAccessViewUint12(wire, list, DescriptorRef{view_heap, 0u}, DescriptorRef{cpu_heap, 0u},
                                                   coverage, Uint4{{0u, 0u, 0u, 0u}}, 0u, nullptr));
    const Barrier settle = {2u, 0u, coverage, 0u, 0u, 0u, 0u, 0u};
    CHECK(av_d3d12_ResourceBarrier12(wire, list, 1u, &settle));
    CHECK(av_d3d12_SetRootSignature12(wire, list, 0u, root));
    CHECK(av_d3d12_SetPipelineState12(wire, list, pipeline));
    CHECK(av_d3d12_SetRootDescriptorTable12(wire, list, 0u, 0u, DescriptorRef{view_heap, 0u}));
    CHECK(av_d3d12_SetTopology12(wire, list, topology_triangle_list));
    const VertexBufferView buffer = {upload_address, static_cast<std::uint32_t>(sizeof rig.positions), 8u};
    CHECK(av_d3d12_SetVertexBuffers12(wire, list, 0u, 1u, &buffer));
    const Viewport viewport = {0.0f, 0.0f, float(size), float(size), 0.0f, 1.0f};
    CHECK(av_d3d12_SetViewports12(wire, list, 1u, &viewport));
    const Rect scissor = {0, 0, int(size), int(size)};
    CHECK(av_d3d12_SetScissorRects12(wire, list, 1u, &scissor));
    CHECK(av_d3d12_SetRenderTargets12(wire, list, 0u, nullptr, 0u, DescriptorRef{}));
    CHECK(av_d3d12_DrawInstanced12(wire, list, triangles * 3u, 1u, 0u, 0u));
    const Barrier to_copy = {0u, 0u, coverage, 0xffffffffu, state_unordered, state_copy_source, 0u, 0u};
    CHECK(av_d3d12_ResourceBarrier12(wire, list, 1u, &to_copy));
    CopyLocation into = {};
    into.resource = readback;
    into.type = 1u;
    into.format = format_r32_uint;
    into.width = size;
    into.height = size;
    into.depth = 1u;
    into.row_pitch = row_pitch;
    CopyLocation from = {};
    from.resource = coverage;
    CHECK(av_d3d12_CopyTextureRegion12(wire, list, into, 0u, 0u, 0u, from, 0u, Box{}));
    const Barrier back = {0u, 0u, coverage, 0xffffffffu, state_copy_source, state_unordered, 0u, 0u};
    CHECK(av_d3d12_ResourceBarrier12(wire, list, 1u, &back));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK(av_d3d12_ResetCommandAllocator12(wire, allocator));
    CHECK(av_d3d12_ResetCommandList12(wire, list, allocator, 0u));
    counts.assign(size * size, 0u);
    for (std::uint32_t y = 0u; y < size; y++)
        std::memcpy(&counts[y * size], wire.memory(rig.readback_memory, std::uint64_t{y} * row_pitch), row_pitch);
    return removed == 0u;
}

struct Expected {
    std::vector<std::uint32_t> counts;
    std::vector<bool>          ambiguous;
};

Expected expect(const Rig &rig, std::uint32_t samples)
{
    constexpr double edge_slack = 1.0 / 256.0;
    Expected       expected;
    expected.counts.assign(size * size, 0u);
    expected.ambiguous.assign(size * size, false);
    const Sample *const offsets = pattern(samples);
    for (std::uint32_t triangle = 0u; triangle < triangles; triangle++) {
        double x[3], y[3];
        for (std::uint32_t vertex = 0u; vertex < 3u; vertex++) {
            x[vertex] = (double(rig.positions[triangle * 6u + vertex * 2u]) + 1.0) * 0.5 * size;
            y[vertex] = (1.0 - double(rig.positions[triangle * 6u + vertex * 2u + 1u])) * 0.5 * size;
        }
        const double area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
        if (std::fabs(area) < 1e-9) continue;
        for (std::uint32_t py = 0u; py < size; py++)
            for (std::uint32_t px = 0u; px < size; px++) {
                std::uint32_t covered = 0u;
                bool          close = false;
                for (std::uint32_t s = 0u; s < samples; s++) {
                    const double sx = px + 0.5 + offsets[s].x / 16.0, sy = py + 0.5 + offsets[s].y / 16.0;
                    bool         inside = true;
                    for (std::uint32_t edge = 0u; edge < 3u; edge++) {
                        const std::uint32_t a = edge, b = (edge + 1u) % 3u;
                        const double        dx = x[b] - x[a], dy = y[b] - y[a];
                        const double        value = (dx * (sy - y[a]) - dy * (sx - x[a])) * (area > 0.0 ? 1.0 : -1.0);
                        if (std::fabs(value) <= edge_slack * std::sqrt(dx * dx + dy * dy)) close = true;
                        if (value < 0.0) inside = false;
                    }
                    covered += inside ? 1u : 0u;
                }
                expected.counts[py * size + px] += covered;
                if (close) expected.ambiguous[py * size + px] = true;
            }
    }
    return expected;
}

void judge(av::D3dLoopback &wire, Rig &rig, std::uint32_t samples)
{
    const std::uint32_t pipeline = first_pipeline + samples;
    CHECK(make_pipeline(wire, pipeline, samples));
    std::vector<std::uint32_t> counts;
    CHECK(draw_counts(wire, rig, pipeline, counts));
    const Expected expected = expect(rig, samples);
    std::uint32_t  wrong = 0u, edges = 0u, total = 0u;
    for (std::uint32_t at = 0u; at < size * size; at++) {
        total += expected.counts[at];
        if (counts[at] == expected.counts[at]) continue;
        if (expected.ambiguous[at]) edges++;
        else wrong++;
    }
    std::fprintf(stderr, "forced %u samples: %u samples covered, %u pixels differ on an edge, %u elsewhere\n", samples,
                 total, edges, wrong);
    CHECK(total > size * size);
    CHECK_EQ(wrong, 0u);
    CHECK(edges <= size);
}

}

TEST(d3d12_forced_coverage_device, uav_only_rendering_counts_the_standard_pattern_of_every_forced_sample_count)
{
    av::ExecutorLogRig    rig_log;
    av::LoaderHostApis    apis(rig_log.log);
    av::D3d11HostDevice  *device11 = nullptr;
    av::D3d12HostDevice  *device12 = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, source)) return;
    av::D3dBackend    backend(rig_log.log);
    av::SessionShares unshared;
    CHECK(backend.initialize(unshared, rig_log.imports, device11, device12));
    av::D3dLoopback wire(backend, rig_log.imports, source, region_bytes, region_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    Rig rig;
    make_scene(wire, rig);
    for (const std::uint32_t samples : {4u, 8u, 16u}) judge(wire, rig, samples);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    backend.shut_down();
}
