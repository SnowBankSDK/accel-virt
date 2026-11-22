#include "fixtures/d3d12/triangle_ps.hpp"
#include "fixtures/d3d12/triangle_vs.hpp"
#include "executor/d3d/d3d_backend.hpp"
#include "executor/host_memory_source.hpp"
#include "executor/owned_fd.hpp"
#include "executor/sharing/host_shares.hpp"
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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

constexpr std::uint32_t size = 64u;
constexpr std::uint32_t row_pitch = size * 4u;
constexpr std::uint32_t format_b8g8r8a8 = 87u;
constexpr std::uint32_t format_r32g32 = 16u;
constexpr std::uint32_t format_r32g32b32a32 = 2u;
constexpr std::uint32_t format_r16_uint = 57u;
constexpr std::uint64_t heap_bytes = 64u * 1024u;
constexpr std::uint64_t readback_address = 0x100000000ull;
constexpr std::uint64_t upload_address = 0x200000000ull;
constexpr std::size_t region_bytes = 16u << 20u;
constexpr std::size_t lent_bytes = 8u << 20u;
constexpr std::uint32_t state_render_target = 0x4u;
constexpr std::uint32_t state_copy_dest = 0x400u;
constexpr std::uint32_t state_copy_source = 0x800u;
constexpr std::uint32_t state_generic_read = 0xac3u;
constexpr std::uint32_t regions = 3u;
constexpr std::uint32_t clear_colour = 0xff0000ffu;

constexpr std::uint64_t vertices_at = 0u;
constexpr std::uint64_t indices_at = 1024u;
constexpr std::uint64_t tints_at = 2048u;
constexpr std::uint64_t arguments_at = 4096u;
constexpr std::uint64_t counts_at = 8192u;

constexpr std::uint32_t argument_draw_indexed = 1u;
constexpr std::uint32_t argument_vertex_buffer_view = 3u;
constexpr std::uint32_t argument_index_buffer_view = 4u;
constexpr std::uint64_t local_indices_at = 1536u;
constexpr std::uint32_t argument_constant = 5u;
constexpr std::uint32_t argument_constant_buffer_view = 6u;
constexpr std::uint32_t parameter_constants = 1u;
constexpr std::uint32_t parameter_cbv = 2u;
constexpr std::uint32_t visibility_pixel = 5u;

enum : std::uint32_t {
    queue = 1u, allocator, list, target, rtv_heap, readback_heap, readback, upload_heap, upload, root, pipeline,
    signature
};

struct Vertex {
    float x, y;
    float r, g, b, a;
};

struct Tint {
    float r, g, b, a;
    std::uint32_t colour;
};

constexpr Tint tints[regions + 1u] = {
    {1.0f, 0.0f, 0.0f, 1.0f, 0xffff0000u},
    {0.0f, 1.0f, 0.0f, 1.0f, 0xff00ff00u},
    {1.0f, 1.0f, 0.0f, 1.0f, 0xffffff00u},
    {1.0f, 1.0f, 1.0f, 1.0f, 0xffffffffu},
};

struct DrawIndexed {
    std::uint32_t index_count, instance_count, first_index;
    std::int32_t base_vertex;
    std::uint32_t first_instance;
};

const char *const wanted_gpu = std::getenv("AV_TEST_HOST_GPU");

bool is_wanted(av::LoaderHostApis &apis, std::uint32_t index)
{
    if (wanted_gpu == nullptr) return true;
    std::uint8_t uuid[16] = {};
    apis.gpu_uuid(index, uuid);
    char text[33] = {};
    for (std::uint32_t at = 0u; at < 16u; at++) std::snprintf(text + 2u * at, 3u, "%02x", uuid[at]);
    return std::strcmp(text, wanted_gpu) == 0;
}

class Withheld {
public:
    Withheld()
    {
        if (std::getenv("AV_D3D_PATH") == nullptr) setenv("AV_D3D_PATH", AV_FIXTURE_DIR "/../../build-d3d/lib", 1);
        setenv("AV_TEST_HOST_WITHOUT", "deviceGeneratedCommands", 1);
    }
    ~Withheld() { unsetenv("AV_TEST_HOST_WITHOUT"); }
};

const av::HostMemoryRequest upload_memory(av::HostMemoryRequest::Kind::upload, 0u);
const av::HostMemoryRequest readback_memory(av::HostMemoryRequest::Kind::readback, 0u);

ResourceDesc buffer_of(std::uint64_t bytes)
{
    ResourceDesc desc = {};
    desc.width = bytes;
    desc.dimension = 1u;
    desc.height = 1u;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.sample_count = 1u;
    desc.layout = 1u;
    return desc;
}

bool imports(av::D3d12HostDevice &device, av::D3dLoopback &wire)
{
    const std::uint64_t memory = wire.open_memory(readback_memory, heap_bytes);
    const av::SourceMemory *const held = wire.source_memory(memory);
    av::OwnedFd shared;
    const bool exported = held != nullptr && held->share(shared);
    ID3D12Heap *const imported = exported ? device.import_dmabuf_heap(held->pointer(), shared.fd(), held->size()) : nullptr;
    wire.close_memory(memory);
    if (imported == nullptr) return false;
    imported->Release();
    return true;
}

class Rig {
public:
    explicit Rig(av::D3dLoopback &wire) : wire_(wire) {}

    void make(std::uint32_t parameter_type, const IndirectArgument &state_argument, std::uint32_t command_bytes)
    {
        const IndirectArgument arguments[1] = {state_argument};
        make(parameter_type, arguments, 1u, command_bytes);
    }

    void make(std::uint32_t parameter_type, const IndirectArgument *state_arguments, std::uint32_t state_count,
              std::uint32_t command_bytes)
    {
        std::uint32_t hresult = 1u;
        CHECK(av_d3d12_OpenDevice12(wire_, 0u, &hresult));
        CHECK_EQ(hresult, 0u);
        CHECK(av_d3d12_CreateCommandQueue12(wire_, queue, 0u, 0u, 0u));
        CHECK(av_d3d12_CreateCommandAllocator12(wire_, allocator, 0u));
        CHECK(av_d3d12_CreateCommandList12(wire_, list, allocator, 0u, 0u));
        CHECK(av_d3d12_CloseCommandList12(wire_, list));

        ResourceDesc texture = {};
        texture.width = size;
        texture.dimension = 3u;
        texture.height = size;
        texture.depth_or_array = 1u;
        texture.mip_levels = 1u;
        texture.format = format_b8g8r8a8;
        texture.sample_count = 1u;
        texture.flags = 0x1u;
        CHECK(av_d3d12_CreateCommittedResource12(wire_, target, HeapProperties{1u, 0u, 0u, 0u}, 0u, texture,
                                                  state_render_target, ClearValue{}, 0u));
        CHECK(av_d3d12_CreateDescriptorHeap12(wire_, rtv_heap, 2u, 1u, 0u));
        ViewDesc view = {};
        view.resource = target;
        CHECK(av_d3d12_CreateRenderTargetView12(wire_, DescriptorRef{rtv_heap, 0u}, 0u, view));

        readback_memory_ = wire_.open_memory(readback_memory, heap_bytes);
        CHECK(readback_memory_ != 0u);
        CHECK(av_d3d12_CreateHeap12(wire_, readback_heap, HeapProperties{3u, 0u, 0u, 0u}, 0u, heap_bytes, 0u,
                                     readback_memory_, 0u));
        CHECK(av_d3d12_CreatePlacedResource12(wire_, readback, readback_heap, 0u, buffer_of(row_pitch * size),
                                               state_copy_dest, ClearValue{}, readback_address));

        upload_memory_ = wire_.open_memory(upload_memory, heap_bytes);
        CHECK(upload_memory_ != 0u);
        CHECK(av_d3d12_CreateHeap12(wire_, upload_heap, HeapProperties{2u, 0u, 0u, 0u}, 0u, heap_bytes, 0u,
                                     upload_memory_, 0u));
        CHECK(av_d3d12_CreatePlacedResource12(wire_, upload, upload_heap, 0u, buffer_of(heap_bytes), state_generic_read,
                                               ClearValue{}, upload_address));

        Vertex vertices[4u * regions] = {};
        std::uint16_t indices[6u * regions] = {};
        for (std::uint32_t k = 0u; k < regions; k++) {
            const float x0 = -1.0f + 2.0f * float(k) / float(regions), x1 = -1.0f + 2.0f * float(k + 1u) / float(regions);
            vertices[4u * k + 0u] = {x0, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
            vertices[4u * k + 1u] = {x0, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
            vertices[4u * k + 2u] = {x1, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
            vertices[4u * k + 3u] = {x1, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
            const std::uint16_t first = static_cast<std::uint16_t>(4u * k);
            const std::uint16_t quad[6] = {first, static_cast<std::uint16_t>(first + 1u), static_cast<std::uint16_t>(first + 2u),
                                           static_cast<std::uint16_t>(first + 2u), static_cast<std::uint16_t>(first + 1u),
                                           static_cast<std::uint16_t>(first + 3u)};
            std::memcpy(indices + 6u * k, quad, sizeof(quad));
        }
        std::memcpy(wire_.memory(upload_memory_, vertices_at), vertices, sizeof(vertices));
        std::memcpy(wire_.memory(upload_memory_, indices_at), indices, sizeof(indices));
        for (std::uint32_t k = 0u; k <= regions; k++)
            std::memcpy(wire_.memory(upload_memory_, local_indices_at + 16u * k), indices, 6u * sizeof(std::uint16_t));
        for (std::uint32_t k = 0u; k <= regions; k++) {
            const float tint[4] = {tints[k].r, tints[k].g, tints[k].b, tints[k].a};
            std::memcpy(wire_.memory(upload_memory_, tints_at + 256u * k), tint, sizeof(tint));
        }

        const RootParameter tint = {parameter_type, visibility_pixel, 0u, 0u, parameter_type == parameter_constants ? 4u : 0u,
                                    0u, 0u, 0u};
        CHECK(av_d3d12_CreateRootSignature12(wire_, root, 0x1u, 1u, &tint, 0u, nullptr, 0u, nullptr));

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
        state.depth_stencil.depth_func = 2u;
        state.topology_type = 3u;
        state.render_targets = 1u;
        state.rtv_formats[0] = format_b8g8r8a8;
        state.sample_count = 1u;
        const InputElement elements[2] = {{0u, 0u, format_r32g32, 0u, 0u, 0u}, {0u, 8u, format_r32g32b32a32, 0u, 0u, 1u}};
        SignatureEntry signatures[8] = {};
        std::uint32_t count = 0u;
        for (const SignatureEntry &entry : triangle_vs::signatures) signatures[count++] = entry;
        for (const SignatureEntry &entry : triangle_ps::signatures) signatures[count++] = entry;
        CHECK(av_d3d12_CreateGraphicsPipeline12(wire_, pipeline, root, state, 2u, elements, count, signatures, 0u,
                                                 nullptr, 0u, nullptr, 0xFFFFFFFFu, triangle_vs::program,
                                                 triangle_vs::program_bytes, nullptr, 0u, nullptr, 0u, nullptr, 0u,
                                                 triangle_ps::program, triangle_ps::program_bytes, nullptr, 0u, nullptr,
                                                 0u, nullptr, 0u));

        IndirectArgument taken[4] = {};
        for (std::uint32_t i = 0u; i < state_count; i++) taken[i] = state_arguments[i];
        taken[state_count] = {argument_draw_indexed, 0u, 0u, 0u};
        command_bytes_ = command_bytes;
        CHECK(av_d3d12_CreateCommandSignature12(wire_, signature, root, command_bytes, state_count + 1u, taken));
        CHECK_EQ(wire_.refused(), 0u);
    }

    void command(std::uint32_t index, const void *state, std::uint32_t state_bytes)
    {
        command(index, state, state_bytes, DrawIndexed{6u, 1u, 6u * (index % regions), 0, 0u});
    }

    void command(std::uint32_t index, const void *state, std::uint32_t state_bytes, const DrawIndexed &draw)
    {
        std::uint8_t *const at = wire_.memory(upload_memory_, arguments_at + std::uint64_t{command_bytes_} * index);
        std::memcpy(at, state, state_bytes);
        std::memcpy(at + state_bytes, &draw, sizeof(draw));
    }

    void count(std::uint32_t value) { std::memcpy(wire_.memory(upload_memory_, counts_at), &value, sizeof(value)); }

    void execute(std::uint32_t max_commands, bool counted, bool set_root_view)
    {
        CHECK(av_d3d12_ResetCommandAllocator12(wire_, allocator));
        CHECK(av_d3d12_ResetCommandList12(wire_, list, allocator, 0u));
        CHECK(av_d3d12_ClearRenderTargetView12(wire_, list, DescriptorRef{rtv_heap, 0u}, Float4{{0.0f, 0.0f, 1.0f, 1.0f}},
                                                0u, nullptr));
        const DescriptorRef targets[1] = {{rtv_heap, 0u}};
        CHECK(av_d3d12_SetRenderTargets12(wire_, list, 1u, targets, 0u, DescriptorRef{}));
        const Viewport viewport = {0.0f, 0.0f, float(size), float(size), 0.0f, 1.0f};
        CHECK(av_d3d12_SetViewports12(wire_, list, 1u, &viewport));
        const Rect scissor = {0, 0, int(size), int(size)};
        CHECK(av_d3d12_SetScissorRects12(wire_, list, 1u, &scissor));
        CHECK(av_d3d12_SetRootSignature12(wire_, list, 0u, root));
        CHECK(av_d3d12_SetPipelineState12(wire_, list, pipeline));
        if (set_root_view) CHECK(av_d3d12_SetRootView12(wire_, list, 0u, 0u, 0u, upload_address + tints_at + 256u * regions));
        const VertexBufferView binding = {upload_address + vertices_at, 4u * regions * sizeof(Vertex), sizeof(Vertex)};
        CHECK(av_d3d12_SetVertexBuffers12(wire_, list, 0u, 1u, &binding));
        CHECK(av_d3d12_SetIndexBuffer12(wire_, list, upload_address + indices_at, 6u * regions * 2u, format_r16_uint));
        CHECK(av_d3d12_SetTopology12(wire_, list, 4u));
        CHECK(av_d3d12_ExecuteIndirect12(wire_, list, signature, max_commands, upload, arguments_at, counted ? upload : 0u,
                                          counted ? counts_at : 0u));

        const Barrier to_copy = {0u, 0u, target, 0xffffffffu, state_render_target, state_copy_source, 0u, 0u};
        CHECK(av_d3d12_ResourceBarrier12(wire_, list, 1u, &to_copy));
        CopyLocation into = {};
        into.resource = readback;
        into.type = 1u;
        into.format = format_b8g8r8a8;
        into.width = size;
        into.height = size;
        into.depth = 1u;
        into.row_pitch = row_pitch;
        CopyLocation from = {};
        from.resource = target;
        CHECK(av_d3d12_CopyTextureRegion12(wire_, list, into, 0u, 0u, 0u, from, 0u, Box{}));
        const Barrier back = {0u, 0u, target, 0xffffffffu, state_copy_source, state_render_target, 0u, 0u};
        CHECK(av_d3d12_ResourceBarrier12(wire_, list, 1u, &back));
        CHECK(av_d3d12_CloseCommandList12(wire_, list));
        const std::uint32_t lists[1] = {list};
        CHECK(av_d3d12_ExecuteCommandLists12(wire_, queue, 1u, lists));
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire_, &removed));
        CHECK_EQ(removed, 0u);
        CHECK_EQ(wire_.refused(), 0u);
    }

    std::uint32_t region(std::uint32_t k) const
    {
        std::uint32_t value = 0u;
        const std::uint32_t x = (2u * k + 1u) * size / (2u * regions);
        std::memcpy(&value, wire_.memory(readback_memory_, (size / 2u) * row_pitch + x * 4u), sizeof(value));
        return value;
    }

private:
    av::D3dLoopback &wire_;
    std::uint64_t    readback_memory_ = 0u;
    std::uint64_t    upload_memory_ = 0u;
    std::uint32_t    command_bytes_ = 0u;
};

template <typename Judge>
void on_each_gpu(Judge judge)
{
    Withheld withheld;
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    apis.open();
    std::uint32_t judged = 0u;
    for (std::uint32_t index = 0u; index < apis.gpu_count(); index++) {
        av::D3d11HostDevice *const device11 = apis.d3d11_device(index);
        av::D3d12HostDevice *const device12 = apis.d3d12_device(index);
        av::HostMemorySource *const source = apis.memory_source(index);
        if (device11 == nullptr || device12 == nullptr || source == nullptr || !is_wanted(apis, index)) continue;
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        session.bind(*apis.shares(index), 1u, backend);
        CHECK(backend.initialize(session, rig.imports, device11, device12));
        {
            av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
            if (imports(*device12, wire) && device12->translates_indirect_addresses()) {
                Rig made(wire);
                judge(made);
                CHECK(backend.invariants_hold());
                judged++;
            }
        }
        backend.shut_down();
    }
    if (judged == 0u) std::fprintf(stderr, "no GPU here runs vkd3d-proton over guest heaps: nothing judged\n");
}

void judge_regions(const Rig &rig, std::uint32_t drawn)
{
    for (std::uint32_t k = 0u; k < regions; k++) {
        const std::uint32_t found = rig.region(k);
        const std::uint32_t expected = k < drawn ? tints[k].colour : clear_colour;
        CHECK_EQ(found, expected);
        if (found != expected) std::fprintf(stderr, "region %u is 0x%08x, expected 0x%08x\n", k, found, expected);
    }
}

void constants_of(std::uint32_t k, float (&out)[4])
{
    out[0] = tints[k].r;
    out[1] = tints[k].g;
    out[2] = tints[k].b;
    out[3] = tints[k].a;
}

}

TEST(d3d12_indirect_state_device, indexed_draws_set_their_own_root_constants_without_device_generated_commands)
{
    on_each_gpu([](Rig &rig) {
        rig.make(parameter_constants, {argument_constant, 0u, 0u, 4u}, 16u + 20u);
        for (std::uint32_t k = 0u; k < regions; k++) {
            float constants[4];
            constants_of(k, constants);
            rig.command(k, constants, sizeof(constants));
        }
        rig.execute(regions, false, false);
        judge_regions(rig, regions);
    });
}

TEST(d3d12_indirect_state_device, a_count_buffer_limits_the_indexed_draws_that_set_root_constants)
{
    on_each_gpu([](Rig &rig) {
        rig.make(parameter_constants, {argument_constant, 0u, 0u, 4u}, 16u + 20u);
        for (std::uint32_t k = 0u; k <= regions; k++) {
            float constants[4];
            constants_of(k, constants);
            rig.command(k, constants, sizeof(constants));
        }
        rig.count(2u);
        rig.execute(regions + 1u, true, false);
        judge_regions(rig, 2u);
        rig.count(7u);
        rig.execute(regions, true, false);
        judge_regions(rig, regions);
    });
}

TEST(d3d12_indirect_state_device, indexed_draws_set_their_own_root_constant_buffer_view_without_device_generated_commands)
{
    on_each_gpu([](Rig &rig) {
        rig.make(parameter_cbv, {argument_constant_buffer_view, 0u, 0u, 0u}, 8u + 20u);
        for (std::uint32_t k = 0u; k < regions; k++) {
            const std::uint64_t view = upload_address + tints_at + 256u * k;
            rig.command(k, &view, sizeof(view));
        }
        rig.execute(regions, false, true);
        judge_regions(rig, regions);
    });
}

TEST(d3d12_indirect_state_device, a_count_buffer_limits_the_indexed_draws_that_set_a_root_constant_buffer_view)
{
    on_each_gpu([](Rig &rig) {
        rig.make(parameter_cbv, {argument_constant_buffer_view, 0u, 0u, 0u}, 8u + 20u);
        for (std::uint32_t k = 0u; k <= regions; k++) {
            const std::uint64_t view = upload_address + tints_at + 256u * k;
            rig.command(k, &view, sizeof(view));
        }
        rig.count(1u);
        rig.execute(regions + 1u, true, true);
        judge_regions(rig, 1u);
    });
}

namespace {

struct ViewsAndConstants {
    std::uint64_t vertex_address;
    std::uint32_t vertex_bytes, vertex_stride;
    std::uint64_t index_address;
    std::uint32_t index_bytes, index_format;
    float tint[4];
};

constexpr IndirectArgument views_and_constants[3] = {
    {argument_vertex_buffer_view, 0u, 0u, 0u},
    {argument_index_buffer_view, 0u, 0u, 0u},
    {argument_constant, 0u, 0u, 4u},
};

void views_command(Rig &rig, std::uint32_t k)
{
    ViewsAndConstants state = {};
    const std::uint32_t region = k % regions;
    state.vertex_address = upload_address + vertices_at + 4u * sizeof(Vertex) * region;
    state.vertex_bytes = 4u * sizeof(Vertex);
    state.vertex_stride = sizeof(Vertex);
    state.index_address = upload_address + local_indices_at + 16u * k;
    state.index_bytes = 6u * sizeof(std::uint16_t);
    state.index_format = format_r16_uint;
    constants_of(k, state.tint);
    rig.command(k, &state, sizeof(state), DrawIndexed{6u, 1u, 0u, 0, 0u});
}

}

TEST(d3d12_indirect_state_device, indexed_draws_set_their_own_vertex_and_index_buffer_views_and_root_constants)
{
    on_each_gpu([](Rig &rig) {
        rig.make(parameter_constants, views_and_constants, 3u, sizeof(ViewsAndConstants) + 20u);
        for (std::uint32_t k = 0u; k < regions; k++) views_command(rig, k);
        rig.execute(regions, false, false);
        judge_regions(rig, regions);
    });
}

TEST(d3d12_indirect_state_device, a_count_buffer_limits_the_indexed_draws_that_set_vertex_and_index_buffer_views)
{
    on_each_gpu([](Rig &rig) {
        rig.make(parameter_constants, views_and_constants, 3u, sizeof(ViewsAndConstants) + 20u);
        for (std::uint32_t k = 0u; k <= regions; k++) views_command(rig, k);
        rig.count(2u);
        rig.execute(regions + 1u, true, false);
        judge_regions(rig, 2u);
    });
}
