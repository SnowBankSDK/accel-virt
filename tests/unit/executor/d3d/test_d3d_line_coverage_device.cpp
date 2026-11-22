#include "fixtures/d3d/triangle_ps.hpp"
#include "fixtures/d3d/triangle_vs.hpp"
#include "fixtures/d3d12/triangle_ps.hpp"
#include "fixtures/d3d12/triangle_vs.hpp"
#include "executor/d3d/d3d_backend.hpp"
#include "executor/host_memory_source.hpp"
#include "executor/sharing/host_shares.hpp"
#include "executor/sharing/session_shares.hpp"
#include "executor/shim/d3d11_host_device.hpp"
#include "executor/shim/d3d12_host_device.hpp"
#include "executor/shim/loader_host_apis.hpp"
#include "executor/source_memory.hpp"
#include "executor/owned_fd.hpp"
#include "harness/executor/d3d12_answers.hpp"
#include "harness/executor/d3d_loopback.hpp"
#include "harness/executor/executor_log_rig.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/generated/d3d/d3d_dispatch_client.h"
#include "proto/generated/d3d12/d3d12_dispatch_client.h"
#include "proto/resources/host_memory_request.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <initializer_list>

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

constexpr std::uint32_t size = 64u;
constexpr std::uint32_t format_b8g8r8a8 = 87u;
constexpr std::uint32_t format_r32g32 = 16u;
constexpr std::uint32_t format_r32g32b32a32 = 2u;
constexpr std::uint32_t topology_lines = 2u;
constexpr std::uint32_t centre_row = 20u;
constexpr std::uint32_t boundary_row = 44u;
constexpr std::uint32_t centre_column = 12u;
constexpr std::uint32_t probe_column = 32u;
constexpr std::uint32_t slanted_column = 8u;
constexpr std::uint32_t slanted_row = 50u;
constexpr std::uint32_t slanted_run = 47u;
constexpr std::uint32_t slanted_rise = 7u;

struct Vertex {
    float x, y;
    float r, g, b, a;
};

constexpr float ndc_x(float window) { return 2.0f * window / float(size) - 1.0f; }
constexpr float ndc_y(float window) { return 1.0f - 2.0f * window / float(size); }

constexpr Vertex line_vertices[8] = {
    {ndc_x(8.0f), ndc_y(float(centre_row) + 0.5f), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(56.0f), ndc_y(float(centre_row) + 0.5f), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(8.0f), ndc_y(float(boundary_row)), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(56.0f), ndc_y(float(boundary_row)), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(float(centre_column) + 0.5f), ndc_y(26.0f), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(float(centre_column) + 0.5f), ndc_y(38.0f), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(float(slanted_column) + 0.5f), ndc_y(float(slanted_row) + 0.5f), 1.0f, 1.0f, 1.0f, 1.0f},
    {ndc_x(float(slanted_column + slanted_run) + 0.5f), ndc_y(float(slanted_row + slanted_rise) + 0.5f), 1.0f, 1.0f, 1.0f,
     1.0f},
};

constexpr std::uint32_t line_vertex_count = sizeof(line_vertices) / sizeof(line_vertices[0]);

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
        setenv("AV_TEST_HOST_WITHOUT", "smoothLines", 1);
    }
    ~Withheld() { unsetenv("AV_TEST_HOST_WITHOUT"); }
};

struct Frame {
    std::uint32_t alpha[size][size];

    void take(const std::uint8_t *rows, std::uint32_t pitch)
    {
        for (std::uint32_t y = 0u; y < size; y++)
            for (std::uint32_t x = 0u; x < size; x++) alpha[y][x] = rows[y * pitch + x * 4u + 3u];
    }

    std::uint32_t partial() const
    {
        std::uint32_t count = 0u;
        for (const auto &row : alpha)
            for (const std::uint32_t a : row) count += a != 0u && a != 255u ? 1u : 0u;
        return count;
    }
};

bool within(std::uint32_t value, std::uint32_t low, std::uint32_t high) { return value >= low && value <= high; }

void judge_antialiased(const Frame &frame)
{
    const std::uint32_t(&a)[size][size] = frame.alpha;
    for (const std::uint32_t x : {16u, probe_column, 48u}) {
        CHECK(a[centre_row][x] >= 250u);
        CHECK(within(a[centre_row - 1u][x], 20u, 235u));
        CHECK(within(a[centre_row + 1u][x], 20u, 235u));
        CHECK(a[centre_row - 1u][x] + 3u >= a[centre_row + 1u][x] && a[centre_row + 1u][x] + 3u >= a[centre_row - 1u][x]);
        CHECK_EQ(a[centre_row - 2u][x], 0u);
        CHECK_EQ(a[centre_row + 2u][x], 0u);
        CHECK(within(a[boundary_row - 1u][x], 120u, 235u));
        CHECK(within(a[boundary_row][x], 120u, 235u));
        CHECK(a[boundary_row - 1u][x] + 3u >= a[boundary_row][x] && a[boundary_row][x] + 3u >= a[boundary_row - 1u][x]);
        CHECK(a[boundary_row - 1u][x] > a[centre_row - 1u][x]);
        CHECK_EQ(a[boundary_row - 2u][x], 0u);
        CHECK_EQ(a[boundary_row + 1u][x], 0u);
    }
    CHECK(a[32u][centre_column] >= 250u);
    CHECK(within(a[32u][centre_column - 1u], 20u, 235u));
    CHECK(within(a[32u][centre_column + 1u], 20u, 235u));
    CHECK_EQ(a[32u][centre_column - 2u], 0u);
    CHECK_EQ(a[32u][centre_column + 2u], 0u);
    if (a[centre_row][probe_column] < 250u || a[centre_row - 1u][probe_column] == 0u)
        std::fprintf(stderr, "antialiased line rows %u..%u at x=%u: %u %u %u %u %u\n", centre_row - 2u, centre_row + 2u,
                     probe_column, a[centre_row - 2u][probe_column], a[centre_row - 1u][probe_column],
                     a[centre_row][probe_column], a[centre_row + 1u][probe_column], a[centre_row + 2u][probe_column]);
}

void judge_aliased(const Frame &frame)
{
    const std::uint32_t(&a)[size][size] = frame.alpha;
    CHECK_EQ(frame.partial(), 0u);
    for (const std::uint32_t x : {16u, probe_column, 48u}) {
        CHECK_EQ(a[centre_row][x], 255u);
        CHECK_EQ(a[centre_row - 1u][x], 0u);
        CHECK_EQ(a[centre_row + 1u][x], 0u);
        CHECK_EQ(a[boundary_row - 1u][x] + a[boundary_row][x], 255u);
    }
    CHECK_EQ(a[32u][centre_column], 255u);
    CHECK_EQ(a[32u][centre_column - 1u], 0u);
    CHECK_EQ(a[32u][centre_column + 1u], 0u);
}

void judge_slanted_aliased(const Frame &frame)
{
    const std::uint32_t(&a)[size][size] = frame.alpha;
    std::uint32_t wrong = 0u;
    for (std::uint32_t step = 0u; step < slanted_run; step++) {
        const std::uint32_t row = slanted_row + (2u * slanted_rise * step + slanted_run) / (2u * slanted_run);
        const std::uint32_t x = slanted_column + step;
        wrong += a[row][x] == 255u && a[row - 1u][x] == 0u && a[row + 1u][x] == 0u ? 0u : 1u;
    }
    CHECK_EQ(wrong, 0u);
}

std::uint64_t fast_linked_pipelines()
{
    char path[512] = {};
    std::snprintf(path, sizeof path, "%s/libdxvk_d3d11.so", std::getenv("AV_D3D_PATH"));
    void *const library = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    CHECK(library != nullptr);
    if (library == nullptr) return 0u;
    const auto count = reinterpret_cast<std::uint64_t (*)()>(dlsym(library, "dxvk_proton_fast_linked_pipelines"));
    CHECK(count != nullptr);
    const std::uint64_t counted = count != nullptr ? count() : 0u;
    dlclose(library);
    return counted;
}

namespace d3d11 {

using namespace av::d3dproto;

constexpr std::uint32_t bind_vertex = 0x1u;
constexpr std::uint32_t bind_constant = 0x4u;
constexpr std::uint32_t bind_render_target = 0x20u;
constexpr std::uint32_t rtv_texture2d = 4u;

enum : std::uint32_t { target = 1u, target_view, vertices, tint, vs, ps, layout, aliased, antialiased, multisampled };

void make(av::D3dLoopback &wire)
{
    ResourceDesc texture = {};
    texture.dimension = 3u;
    texture.width = size;
    texture.height = size;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_b8g8r8a8;
    texture.sample_count = 1u;
    texture.bind_flags = bind_render_target;
    CHECK(av_d3d_CreateResource(wire, target, texture, 0u, 0u, 0u));
    ViewDesc rtv = {};
    rtv.resource = target;
    rtv.format = format_b8g8r8a8;
    rtv.dimension = rtv_texture2d;
    CHECK(av_d3d_CreateRenderTargetView(wire, target_view, rtv));

    ResourceDesc buffer = {};
    buffer.dimension = 1u;
    buffer.width = sizeof(line_vertices);
    buffer.bind_flags = bind_vertex;
    CHECK(av_d3d_CreateResource(wire, vertices, buffer, 0u, 0u, 0u));
    CHECK(av_d3d_UpdateSubresource(wire, vertices, 0u, 0u, Box{}, 0u, 0u, 0u, line_vertices, sizeof(line_vertices)));
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    buffer.width = sizeof(white);
    buffer.bind_flags = bind_constant;
    CHECK(av_d3d_CreateResource(wire, tint, buffer, 0u, 0u, 0u));
    CHECK(av_d3d_UpdateSubresource(wire, tint, 0u, 0u, Box{}, 0u, 0u, 0u, white, sizeof(white)));

    CHECK(av_d3d_CreateShader(wire, vs, 0u, triangle_vs::inputs_count, triangle_vs::inputs, triangle_vs::outputs_count,
                               triangle_vs::outputs, 0u, nullptr, 0u, nullptr, 0u, nullptr, 0xffffffffu,
                               triangle_vs::tokens, sizeof(triangle_vs::tokens)));
    CHECK(av_d3d_CreateShader(wire, ps, 4u, triangle_ps::inputs_count, triangle_ps::inputs, triangle_ps::outputs_count,
                               triangle_ps::outputs, 0u, nullptr, 0u, nullptr, 0u, nullptr, 0xffffffffu,
                               triangle_ps::tokens, sizeof(triangle_ps::tokens)));
    const InputElement elements[2] = {{0u, 0u, format_r32g32, 0u, 0u, 0u}, {0u, 8u, format_r32g32b32a32, 0u, 0u, 1u}};
    CHECK(av_d3d_CreateInputLayout(wire, layout, 2u, elements));

    RasterizerDesc desc = {};
    desc.fill_mode = 3u;
    desc.cull_mode = 1u;
    desc.depth_clip = 1u;
    CHECK(av_d3d_CreateRasterizerState(wire, aliased, desc));
    desc.antialiased_line = 1u;
    CHECK(av_d3d_CreateRasterizerState(wire, antialiased, desc));
    desc.multisample = 1u;
    CHECK(av_d3d_CreateRasterizerState(wire, multisampled, desc));
}

void draw(av::D3dLoopback &wire, std::uint32_t rasterizer, Frame &frame)
{
    const std::uint32_t targets[1] = {target_view};
    CHECK(av_d3d_SetRenderTargets(wire, 1u, targets, 0u, 1u, 0u, nullptr));
    CHECK(av_d3d_ClearRenderTargetView(wire, target_view, Float4{{0.0f, 0.0f, 0.0f, 0.0f}}));
    const Viewport viewport = {0.0f, 0.0f, float(size), float(size), 0.0f, 1.0f};
    CHECK(av_d3d_SetViewports(wire, 1u, &viewport));
    CHECK(av_d3d_SetInputLayout(wire, layout));
    const VertexBufferBinding binding = {vertices, sizeof(Vertex), 0u};
    CHECK(av_d3d_SetVertexBuffers(wire, 0u, 1u, &binding));
    CHECK(av_d3d_SetTopology(wire, topology_lines));
    CHECK(av_d3d_SetShader(wire, 0u, vs));
    CHECK(av_d3d_SetShader(wire, 4u, ps));
    const ConstantBufferBinding constants = {tint, 0u, 0u};
    CHECK(av_d3d_SetConstantBuffers(wire, 4u, 0u, 1u, &constants));
    CHECK(av_d3d_SetRasterizerState(wire, rasterizer));
    CHECK(av_d3d_Draw(wire, line_vertex_count, 0u));

    static std::uint8_t pixels[size * size * 4u];
    std::uint32_t hresult = 1u, row_pitch = 0u, depth_pitch = 0u;
    std::uint64_t read = 0u;
    CHECK(av_d3d_ReadSubresource(wire, target, 0u, &hresult, &row_pitch, &depth_pitch, pixels, sizeof(pixels), &read));
    CHECK_EQ(hresult, 0u);
    CHECK_EQ(row_pitch, size * 4u);
    frame.take(pixels, row_pitch);
}

template <typename Judge>
void on_each_gpu(Judge judge)
{
    Withheld withheld;
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    apis.open();
    std::uint32_t judged = 0u;
    for (std::uint32_t index = 0u; index < apis.gpu_count(); index++) {
        av::D3d11HostDevice *const device = apis.d3d11_device(index);
        if (device == nullptr || !is_wanted(apis, index)) continue;
        av::D3dBackend backend(rig.log);
        av::SessionShares unshared;
        CHECK(backend.initialize(unshared, rig.imports, device));
        {
            av::D3dLoopback wire(backend, rig.imports);
            std::uint32_t hresult = 1u, level = 0u;
            CHECK(av_d3d_CreateDevice11(wire, 1u, 0u, 0u, &hresult, &level));
            CHECK_EQ(hresult, 0u);
            make(wire);
            judge(wire);
            CHECK_EQ(wire.refused(), 0u);
            CHECK(backend.invariants_hold());
        }
        backend.shut_down();
        judged++;
    }
    if (judged == 0u) std::fprintf(stderr, "no GPU here runs DXVK: nothing judged\n");
}

}

namespace d3d12 {

using namespace av::d3d12proto;

constexpr std::uint32_t row_pitch = size * 4u;
constexpr std::uint64_t heap_bytes = 64u * 1024u;
constexpr std::uint64_t readback_address = 0x100000000ull;
constexpr std::uint64_t upload_address = 0x200000000ull;
constexpr std::size_t region_bytes = 16u << 20u;
constexpr std::size_t lent_bytes = 8u << 20u;
constexpr std::uint32_t state_render_target = 0x4u;
constexpr std::uint32_t state_copy_dest = 0x400u;
constexpr std::uint32_t state_copy_source = 0x800u;
constexpr std::uint32_t state_generic_read = 0xac3u;

enum : std::uint32_t {
    queue = 1u, allocator, list, target, rtv_heap, readback_heap, readback, upload_heap, vertices, root,
    aliased_pipeline, antialiased_pipeline
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

struct Rig {
    std::uint64_t readback_memory_id = 0u;
    std::uint64_t upload_memory_id = 0u;
};

void make_pipeline(av::D3dLoopback &wire, std::uint32_t id, std::uint32_t line_mode)
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
    state.rasterizer.line_mode = line_mode;
    state.depth_stencil.depth_func = 2u;
    state.topology_type = 2u;
    state.render_targets = 1u;
    state.rtv_formats[0] = format_b8g8r8a8;
    state.sample_count = 1u;
    const InputElement elements[2] = {{0u, 0u, format_r32g32, 0u, 0u, 0u}, {0u, 8u, format_r32g32b32a32, 0u, 0u, 1u}};
    SignatureEntry signatures[8] = {};
    std::uint32_t count = 0u;
    for (const SignatureEntry &entry : triangle_vs::signatures) signatures[count++] = entry;
    for (const SignatureEntry &entry : triangle_ps::signatures) signatures[count++] = entry;
    CHECK(av_d3d12_CreateGraphicsPipeline12(wire, id, root, state, 2u, elements, count, signatures, 0u, nullptr, 0u,
                                             nullptr, 0xFFFFFFFFu, triangle_vs::program, triangle_vs::program_bytes,
                                             nullptr, 0u, nullptr, 0u, nullptr, 0u, triangle_ps::program,
                                             triangle_ps::program_bytes, nullptr, 0u, nullptr, 0u, nullptr, 0u));
}

void make(av::D3dLoopback &wire, Rig &rig)
{
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateCommandQueue12(wire, queue, 0u, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, allocator, 0u));
    CHECK(av_d3d12_CreateCommandList12(wire, list, allocator, 0u, 0u));
    CHECK(av_d3d12_CloseCommandList12(wire, list));

    ResourceDesc texture = {};
    texture.width = size;
    texture.dimension = 3u;
    texture.height = size;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_b8g8r8a8;
    texture.sample_count = 1u;
    texture.flags = 0x1u;
    CHECK(av_d3d12_CreateCommittedResource12(wire, target, HeapProperties{1u, 0u, 0u, 0u}, 0u, texture,
                                              state_render_target, ClearValue{}, 0u));
    CHECK(av_d3d12_CreateDescriptorHeap12(wire, rtv_heap, 2u, 1u, 0u));
    ViewDesc view = {};
    view.resource = target;
    CHECK(av_d3d12_CreateRenderTargetView12(wire, DescriptorRef{rtv_heap, 0u}, 0u, view));

    rig.readback_memory_id = wire.open_memory(readback_memory, heap_bytes);
    CHECK(rig.readback_memory_id != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, readback_heap, HeapProperties{3u, 0u, 0u, 0u}, 0u, heap_bytes, 0u,
                                 rig.readback_memory_id, 0u));
    CHECK(av_d3d12_CreatePlacedResource12(wire, readback, readback_heap, 0u, buffer_of(row_pitch * size),
                                           state_copy_dest, ClearValue{}, readback_address));

    rig.upload_memory_id = wire.open_memory(upload_memory, heap_bytes);
    CHECK(rig.upload_memory_id != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, HeapProperties{2u, 0u, 0u, 0u}, 0u, heap_bytes, 0u,
                                 rig.upload_memory_id, 0u));
    std::memcpy(wire.memory(rig.upload_memory_id), line_vertices, sizeof(line_vertices));
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(wire.memory(rig.upload_memory_id, 256u), white, sizeof(white));
    CHECK(av_d3d12_CreatePlacedResource12(wire, vertices, upload_heap, 0u, buffer_of(512u), state_generic_read,
                                           ClearValue{}, upload_address));

    const RootParameter tint_view = {2u, 5u, 0u, 0u, 0u, 0u, 0u, 0u};
    CHECK(av_d3d12_CreateRootSignature12(wire, root, 0x1u, 1u, &tint_view, 0u, nullptr, 0u, nullptr));
    make_pipeline(wire, aliased_pipeline, 0u);
    make_pipeline(wire, antialiased_pipeline, 1u);
}

void draw(av::D3dLoopback &wire, const Rig &rig, std::uint32_t pipeline, Frame &frame)
{
    CHECK(av_d3d12_ResetCommandAllocator12(wire, allocator));
    CHECK(av_d3d12_ResetCommandList12(wire, list, allocator, 0u));
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{rtv_heap, 0u}, Float4{{0.0f, 0.0f, 0.0f, 0.0f}},
                                            0u, nullptr));
    const DescriptorRef targets[1] = {{rtv_heap, 0u}};
    CHECK(av_d3d12_SetRenderTargets12(wire, list, 1u, targets, 0u, DescriptorRef{}));
    const Viewport viewport = {0.0f, 0.0f, float(size), float(size), 0.0f, 1.0f};
    CHECK(av_d3d12_SetViewports12(wire, list, 1u, &viewport));
    const Rect scissor = {0, 0, int(size), int(size)};
    CHECK(av_d3d12_SetScissorRects12(wire, list, 1u, &scissor));
    CHECK(av_d3d12_SetRootSignature12(wire, list, 0u, root));
    CHECK(av_d3d12_SetPipelineState12(wire, list, pipeline));
    CHECK(av_d3d12_SetRootView12(wire, list, 0u, 0u, 0u, upload_address + 256u));
    const VertexBufferView binding = {upload_address, sizeof(line_vertices), sizeof(Vertex)};
    CHECK(av_d3d12_SetVertexBuffers12(wire, list, 0u, 1u, &binding));
    CHECK(av_d3d12_SetTopology12(wire, list, topology_lines));
    CHECK(av_d3d12_DrawInstanced12(wire, list, line_vertex_count, 1u, 0u, 0u));

    const Barrier to_copy = {0u, 0u, target, 0xffffffffu, state_render_target, state_copy_source, 0u, 0u};
    CHECK(av_d3d12_ResourceBarrier12(wire, list, 1u, &to_copy));
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
    CHECK(av_d3d12_CopyTextureRegion12(wire, list, into, 0u, 0u, 0u, from, 0u, Box{}));
    const Barrier back = {0u, 0u, target, 0xffffffffu, state_copy_source, state_render_target, 0u, 0u};
    CHECK(av_d3d12_ResourceBarrier12(wire, list, 1u, &back));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    frame.take(wire.memory(rig.readback_memory_id), row_pitch);
}

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
            if (imports(*device12, wire)) {
                Rig made;
                make(wire, made);
                CHECK_EQ(wire.refused(), 0u);
                judge(wire, made);
                CHECK_EQ(wire.refused(), 0u);
                CHECK(backend.invariants_hold());
                judged++;
            }
        }
        backend.shut_down();
    }
    if (judged == 0u) std::fprintf(stderr, "no GPU here runs vkd3d-proton over guest heaps: nothing judged\n");
}

}

}

TEST(d3d_line_coverage_device, a_direct3d11_antialiased_line_is_full_at_its_centre_and_falls_off_at_its_edges)
{
    d3d11::on_each_gpu([](av::D3dLoopback &wire) {
        Frame frame;
        d3d11::draw(wire, d3d11::antialiased, frame);
        judge_antialiased(frame);
    });
}

TEST(d3d_line_coverage_device, the_first_aliased_line_of_a_direct3d11_device_lights_the_diamond_exit_pixels_and_no_fast_linked_pipeline_draws_it)
{
    d3d11::on_each_gpu([](av::D3dLoopback &wire) {
        const std::uint64_t fast_linked = fast_linked_pipelines();
        Frame               frame;
        d3d11::draw(wire, d3d11::aliased, frame);
        judge_aliased(frame);
        judge_slanted_aliased(frame);
        CHECK_EQ(fast_linked_pipelines(), fast_linked);
    });
}

TEST(d3d_line_coverage_device, direct3d11_aliased_and_multisample_lines_keep_whole_pixels)
{
    d3d11::on_each_gpu([](av::D3dLoopback &wire) {
        Frame frame;
        d3d11::draw(wire, d3d11::aliased, frame);
        judge_aliased(frame);
        d3d11::draw(wire, d3d11::antialiased, frame);
        CHECK(frame.partial() > 0u);
        d3d11::draw(wire, d3d11::aliased, frame);
        judge_aliased(frame);
        d3d11::draw(wire, d3d11::multisampled, frame);
        CHECK_EQ(frame.partial(), 0u);
    });
}

TEST(d3d_line_coverage_device, a_direct3d12_alpha_antialiased_line_is_full_at_its_centre_and_falls_off_at_its_edges)
{
    d3d12::on_each_gpu([](av::D3dLoopback &wire, const d3d12::Rig &rig) {
        Frame frame;
        d3d12::draw(wire, rig, d3d12::antialiased_pipeline, frame);
        judge_antialiased(frame);
    });
}

TEST(d3d_line_coverage_device, direct3d12_aliased_lines_keep_whole_pixels_after_antialiased_ones)
{
    d3d12::on_each_gpu([](av::D3dLoopback &wire, const d3d12::Rig &rig) {
        Frame frame;
        d3d12::draw(wire, rig, d3d12::aliased_pipeline, frame);
        judge_aliased(frame);
        d3d12::draw(wire, rig, d3d12::antialiased_pipeline, frame);
        CHECK(frame.partial() > 0u);
        d3d12::draw(wire, rig, d3d12::aliased_pipeline, frame);
        judge_aliased(frame);
    });
}
