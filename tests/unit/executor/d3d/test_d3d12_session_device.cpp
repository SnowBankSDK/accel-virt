#include "core/log/log_cadence.hpp"
#include "executor/d3d/shared_surfaces.hpp"
#include "fixtures/d3d12/triangle_ps.hpp"
#include "fixtures/d3d12/triangle_vs.hpp"
#include "core/log/driver_log.hpp"
#include "executor/d3d/d3d12_block_heaps.hpp"
#include "executor/d3d/d3d12_queue_progress.hpp"
#include "executor/d3d/d3d_backend.hpp"
#include "executor/eventfd_wake.hpp"
#include "harness/platform/mock_platform.hpp"
#include "executor/host_memory_source.hpp"
#include "executor/owned_fd.hpp"
#include "executor/import_table.hpp"
#include "executor/imported_blob.hpp"
#include "proto/submission/blob_role.hpp"
#include "executor/source_memory.hpp"
#include "executor/shim/d3d11_host_device.hpp"
#include "executor/shim/d3d12_host_device.hpp"
#include "executor/shim/d3d_headers.hpp"
#include "executor/sharing/host_shares.hpp"
#include "executor/sharing/session_shares.hpp"
#include "executor/shim/loader_host_apis.hpp"
#include "harness/executor/d3d12_answers.hpp"
#include "harness/executor/d3d_loopback.hpp"
#include "harness/executor/local_import.hpp"
#include "executor/shim/host_heap.hpp"
#include "harness/executor/executor_log_rig.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/generated/d3d/d3d_dispatch_client.h"
#include "proto/generated/d3d12/d3d12_dispatch_client.h"
#include "proto/d3d/heap_slice_support.hpp"
#include "proto/resources/host_memory_request.hpp"
#include "proto/resources/surface_point.hpp"

#include <cstdint>
#include <initializer_list>
#include <cstdio>
#include "proto/d3d/caps12_layout.hpp"

#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

using namespace av::d3d12proto;

constexpr std::uint32_t format_b8g8r8a8 = 87u;
constexpr std::uint32_t format_r32g32 = 16u;
constexpr std::uint32_t format_r32g32b32a32 = 2u;
constexpr std::uint32_t size = 64u;
constexpr std::uint32_t row_pitch = size * 4u;
constexpr std::uint32_t heap_default = 1u;
constexpr std::uint32_t heap_upload = 2u;
constexpr std::uint32_t heap_readback = 3u;
constexpr std::uint32_t state_render_target = 0x4u;
constexpr std::uint32_t state_copy_dest = 0x400u;
constexpr std::uint32_t state_copy_source = 0x800u;
constexpr std::uint32_t state_generic_read = 0xac3u;
constexpr std::uint32_t flag_render_target = 0x1u;
constexpr std::uint32_t descriptors_rtv = 2u;
constexpr std::uint32_t dimension_buffer = 1u;
constexpr std::uint32_t dimension_texture2d = 3u;
constexpr std::uint32_t layout_row_major = 1u;
constexpr std::uint32_t topology_type_triangle = 3u;
constexpr std::uint32_t topology_triangles = 4u;
constexpr std::uint32_t visibility_pixel = 5u;
constexpr std::uint32_t root_cbv = 2u;
constexpr std::uint32_t allow_input_layout = 0x1u;
constexpr std::uint32_t driver_internal_error = 0x887A0020u;

constexpr std::size_t   region_bytes = 16u << 20u;
constexpr std::size_t   lent_bytes = 8u << 20u;
constexpr std::uint64_t heap_bytes = 64u * 1024u;
constexpr std::uint64_t readback_address = 0x100000000ull;
constexpr std::uint64_t upload_address = 0x200000000ull;

const av::HostMemoryRequest upload_memory(av::HostMemoryRequest::Kind::upload, 0u);
const av::HostMemoryRequest readback_memory(av::HostMemoryRequest::Kind::readback, 0u);

enum Id : std::uint32_t {
    queue = 1, allocator, list, target, rtv_heap, readback_heap, readback, upload_heap, vertices, constants,
    root, pipeline, signature, arguments, counts, shown, shown_rtv
};

struct HeapMemory {
    std::uint64_t readback;
    std::uint64_t upload;
};
HeapMemory heaps = {};
std::uint8_t *readback_native = nullptr;
std::uint8_t *upload_native = nullptr;

struct Vertex {
    float x, y;
    float r, g, b, a;
};

bool host_devices(av::LoaderHostApis &apis, av::D3d11HostDevice *&device11, av::D3d12HostDevice *&device12,
                  av::HostShares *&shares, av::HostMemorySource *&source)
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
        shares = apis.shares(index);
        source = apis.memory_source(index);
        if (device11 != nullptr && device12 != nullptr && source != nullptr) return true;
    }
    std::fprintf(stderr, "no GPU here runs vkd3d-proton: nothing judged\n");
    return false;
}

av::SessionShares *shared_as(av::SessionShares &session, av::HostShares *shares, av::D3dBackend &backend)
{
    session.bind(*shares, 1u, backend);
    return &session;
}

bool imports(av::D3d12HostDevice &device, av::D3dLoopback &wire)
{
    const std::uint64_t        memory = wire.open_memory(readback_memory, heap_bytes);
    const av::SourceMemory *const held = wire.source_memory(memory);
    av::OwnedFd                shared;
    const bool                 exported = held != nullptr && held->share(shared);
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

HeapProperties heap_of(std::uint32_t type)
{
    return HeapProperties{type, 0u, 0u, 0u};
}

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

bool place_heap(av::D3dLoopback &wire, std::uint32_t id, std::uint32_t type, std::uint8_t *&view)
{
    std::uint32_t hresult = 1u;
    std::uint64_t placed = 0u;
    if (!av_d3d12_CreateHeap12(wire, id, heap_of(type), 0u, heap_bytes, 0u, 0u, 0u, &hresult, &placed)) return false;
    if (hresult != 0u || placed != id) return false;
    view = wire.take_placed_heap(heap_bytes);
    return view != nullptr;
}

void make_frame(av::D3dLoopback &wire, bool native = false)
{
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    Caps12        caps = {};
    std::uint8_t  formats[4096] = {};
    std::uint64_t formats_size = 0u;
    hresult = 1u;
    CHECK(av_d3d12_QueryCaps12(wire, &hresult, &caps, formats, sizeof formats, &formats_size));
    CHECK_EQ(hresult, 0u);
    CHECK_EQ(caps.layout, av::caps12_layout);
    CHECK(caps.feature_level >= 0xb000u);
    CHECK(caps.descriptor_sizes[descriptors_rtv] != 0u);
    CHECK(formats_size != 0u);
    CHECK(av_d3d12_CreateCommandQueue12(wire, queue, 0u, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, allocator, 0u));
    CHECK(av_d3d12_CreateCommandList12(wire, list, allocator, 0u, 0u));

    ResourceDesc texture = {};
    texture.width = size;
    texture.dimension = dimension_texture2d;
    texture.height = size;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_b8g8r8a8;
    texture.sample_count = 1u;
    texture.flags = flag_render_target;
    std::uint64_t bytes = 0u, alignment = 0u;
    CHECK(av_d3d12_ResourceAllocation12(wire, texture, &bytes, &alignment));
    CHECK(bytes >= std::uint64_t{size} * size * 4u);
    CHECK(alignment != 0u && (alignment & (alignment - 1u)) == 0u);
    ResourceDesc refused = texture;
    refused.dimension = 9u;
    CHECK(av_d3d12_ResourceAllocation12(wire, refused, &bytes, &alignment));
    CHECK_EQ(bytes, ~std::uint64_t{0});
    CHECK(caps.wave_lane_min >= 4u && caps.wave_lane_min <= caps.wave_lane_max);
    CHECK(caps.total_lanes >= caps.wave_lane_max);
    CHECK(av_d3d12_CreateCommittedResource12(wire, target, heap_of(heap_default), 0u, texture, state_render_target,
                                              ClearValue{}, 0u));
    CHECK(av_d3d12_CreateDescriptorHeap12(wire, rtv_heap, descriptors_rtv, 1u, 0u));
    ViewDesc view = {};
    view.resource = target;
    CHECK(av_d3d12_CreateRenderTargetView12(wire, DescriptorRef{rtv_heap, 0u}, 0u, view));

    if (native) {
        CHECK(place_heap(wire, readback_heap, heap_readback, readback_native));
        std::memset(readback_native, 0xcd, heap_bytes);
    } else {
        heaps.readback = wire.open_memory(readback_memory, heap_bytes);
        CHECK(heaps.readback != 0u);
        std::memset(wire.memory(heaps.readback), 0xcd, heap_bytes);
        CHECK(av_d3d12_CreateHeap12(wire, readback_heap, heap_of(heap_readback), 0u, heap_bytes, 0u, heaps.readback, 0u));
    }
    CHECK(av_d3d12_CreatePlacedResource12(wire, readback, readback_heap, 0u, buffer_of(row_pitch * size),
                                           state_copy_dest, ClearValue{}, readback_address));
}

void read_frame(av::D3dLoopback &wire)
{
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
}

std::uint32_t pixel(const av::D3dLoopback &wire, std::uint32_t x, std::uint32_t y)
{
    std::uint32_t value = 0u;
    const std::uint8_t *const at = readback_native != nullptr ? readback_native + y * row_pitch + x * 4u
                                                              : wire.memory(heaps.readback, y * row_pitch + x * 4u);
    std::memcpy(&value, at, sizeof(value));
    return value;
}

void make_triangle(av::D3dLoopback &wire, bool whole_containers, bool native = false)
{
    if (native) {
        CHECK(place_heap(wire, upload_heap, heap_upload, upload_native));
    } else {
        heaps.upload = wire.open_memory(upload_memory, heap_bytes);
        CHECK(heaps.upload != 0u);
        CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, heaps.upload, 0u));
    }
    const Vertex triangle[3] = {{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
                                {-1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
                                {0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f}};
    std::uint8_t *const upload_view = native ? upload_native : wire.memory(heaps.upload);
    std::memcpy(upload_view, triangle, sizeof(triangle));
    const float tint[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    std::memcpy(upload_view + 256u, tint, sizeof(tint));
    CHECK(av_d3d12_CreatePlacedResource12(wire, vertices, upload_heap, 0u, buffer_of(256u), state_generic_read,
                                           ClearValue{}, upload_address));
    CHECK(av_d3d12_CreatePlacedResource12(wire, constants, upload_heap, 256u, buffer_of(256u), state_generic_read,
                                           ClearValue{}, upload_address + 256u));

    const RootParameter tint_view = {root_cbv, visibility_pixel, 0u, 0u, 0u, 0u, 0u, 0u};
    CHECK(av_d3d12_CreateRootSignature12(wire, root, allow_input_layout, 1u, &tint_view, 0u, nullptr, 0u, nullptr));

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
    state.topology_type = topology_type_triangle;
    state.render_targets = 1u;
    state.rtv_formats[0] = format_b8g8r8a8;
    state.sample_count = 1u;
    const InputElement elements[2] = {{0u, 0u, format_r32g32, 0u, 0u, 0u}, {0u, 8u, format_r32g32b32a32, 0u, 0u, 1u}};
    SignatureEntry signatures[8] = {};
    std::uint32_t  count = 0u;
    for (const SignatureEntry &entry : triangle_vs::signatures) signatures[count++] = entry;
    for (const SignatureEntry &entry : triangle_ps::signatures) signatures[count++] = entry;
    const void *const vs = whole_containers ? static_cast<const void *>(triangle_vs::container)
                                            : static_cast<const void *>(triangle_vs::program);
    const void *const ps = whole_containers ? static_cast<const void *>(triangle_ps::container)
                                            : static_cast<const void *>(triangle_ps::program);
    const std::uint64_t vs_bytes = whole_containers ? triangle_vs::container_bytes : triangle_vs::program_bytes;
    const std::uint64_t ps_bytes = whole_containers ? triangle_ps::container_bytes : triangle_ps::program_bytes;
    CHECK(av_d3d12_CreateGraphicsPipeline12(wire, pipeline, root, state, 2u, elements, whole_containers ? 0u : count,
                                             signatures, 0u, nullptr, 0u, nullptr, 0xFFFFFFFFu, vs, vs_bytes, nullptr, 0u, nullptr, 0u, nullptr, 0u, ps,
                                             ps_bytes, nullptr, 0u, nullptr, 0u));
}

void bind_triangle(av::D3dLoopback &wire)
{
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{rtv_heap, 0u}, Float4{{0.0f, 0.0f, 1.0f, 1.0f}},
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
    const VertexBufferView binding = {upload_address, 3u * sizeof(Vertex), sizeof(Vertex)};
    CHECK(av_d3d12_SetVertexBuffers12(wire, list, 0u, 1u, &binding));
    CHECK(av_d3d12_SetTopology12(wire, list, topology_triangles));
}

void draw_triangle(av::D3dLoopback &wire)
{
    bind_triangle(wire);
    CHECK(av_d3d12_DrawInstanced12(wire, list, 3u, 1u, 0u, 0u));
}

void judge_triangle(const av::D3dLoopback &wire)
{
    const std::uint32_t tinted = pixel(wire, 4u, size / 2u);
    CHECK_EQ(tinted & 0xff00ffffu, 0xff000000u);
    CHECK((tinted >> 16u & 0xffu) >= 0x7fu && (tinted >> 16u & 0xffu) <= 0x80u);
    CHECK_EQ(pixel(wire, size - 4u, size / 2u), 0xff0000ffu);
}

}

TEST(d3d12_session_device, a_cleared_target_reads_back_through_guest_memory)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    make_frame(wire);

    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{rtv_heap, 0u}, Float4{{1.0f, 0.0f, 0.0f, 1.0f}},
                                            0u, nullptr));
    read_frame(wire);
    CHECK_EQ(pixel(wire, 0u, 0u), 0xffff0000u);
    CHECK_EQ(pixel(wire, size - 1u, size - 1u), 0xffff0000u);
    CHECK_EQ(wire.refused(), 0u);

    CHECK(av_d3d12_ResetCommandAllocator12(wire, allocator));
    CHECK(av_d3d12_ResetCommandList12(wire, list, allocator, 0u));
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{rtv_heap, 0u}, Float4{{0.0f, 1.0f, 0.0f, 1.0f}},
                                            0u, nullptr));
    read_frame(wire);
    CHECK_EQ(pixel(wire, size / 2u, size / 2u), 0xff00ff00u);

    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK(av_d3d12_CloseCommandList12(wire, 77u));
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{rtv_heap, 5u}, Float4{}, 0u, nullptr));
    CHECK(av_d3d12_SetRootView12(wire, list, 0u, 0u, 0u, 0x999u));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, driver_internal_error);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

TEST(d3d12_session_device, a_triangle_drawn_from_ddi_dxil_reads_back_where_it_was_drawn)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    for (int whole = 0; whole < 2; whole++) {
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        if (!imports(*device12, wire)) {
            backend.shut_down();
            return;
        }
        make_frame(wire);
        make_triangle(wire, whole != 0);
        CHECK_EQ(wire.refused(), 0u);
        draw_triangle(wire);
        read_frame(wire);
        judge_triangle(wire);
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, 0u);
        CHECK_EQ(wire.refused(), 0u);
        CHECK(backend.invariants_hold());
        backend.shut_down();
    }
}

TEST(d3d12_session_device, native_upload_and_readback_heaps_are_host_memory_the_guest_maps_and_a_triangle_drawn_from_them_reads_back)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    if (!device12->exports_heaps()) {
        std::fprintf(stderr, "this vkd3d-proton exports no heap (tools/vkd3d-proton-patches): nothing judged\n");
        return;
    }
    setenv("AV_HEAP_PLACEMENT", "native", 1);
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12, nullptr, nullptr, source));
    unsetenv("AV_HEAP_PLACEMENT");
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    std::uint32_t placement = 0u;
    CHECK(av_d3d12_QueryHeapPlacement12(wire, &placement));
    CHECK_EQ(placement, 1u);
    std::uint32_t slices = 7u;
    CHECK(av_d3d12_QueryHeapSlices12(wire, &slices));
    CHECK_EQ(slices, static_cast<std::uint32_t>(av::HeapSliceSupport::none));
    readback_native = nullptr;
    upload_native = nullptr;
    make_frame(wire, true);
    make_triangle(wire, false, true);
    CHECK_EQ(wire.refused(), 0u);
    CHECK_EQ(source->placed(), 0u);
    draw_triangle(wire);
    read_frame(wire);
    judge_triangle(wire);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
    readback_native = nullptr;
    upload_native = nullptr;
}

TEST(d3d12_session_device, a_native_heap_the_guest_never_maps_is_dropped_with_its_heap_and_a_host_that_imports_places_nothing)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    if (!device12->exports_heaps()) return;
    for (int native = 0; native < 2; native++) {
        setenv("AV_HEAP_PLACEMENT", native != 0 ? "native" : "import", 1);
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12, nullptr, nullptr, source));
        unsetenv("AV_HEAP_PLACEMENT");
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        std::uint32_t placement = 7u;
        CHECK(av_d3d12_QueryHeapPlacement12(wire, &placement));
        CHECK_EQ(placement, native != 0 ? 1u : 0u);
        std::uint32_t hresult = 1u;
        std::uint64_t placed = 9u;
        CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
        if (native != 0) {
            CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, 0u, 0u, &hresult, &placed));
            CHECK_EQ(hresult, 0u);
            CHECK_EQ(placed, std::uint64_t{upload_heap});
            CHECK_EQ(source->placed(), 1u);
            CHECK(av_d3d12_Destroy12(wire, upload_heap));
            CHECK_EQ(source->placed(), 0u);
        }
        backend.shut_down();
    }
}

TEST(d3d12_session_device, a_session_on_a_host_that_imports_no_heap_refuses_a_heap_over_guest_memory_and_keeps_working)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    if (!device12->exports_heaps()) return;
    setenv("AV_HEAP_PLACEMENT", "native", 1);
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12, nullptr, nullptr, source));
    unsetenv("AV_HEAP_PLACEMENT");
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    readback_native = nullptr;
    upload_native = nullptr;
    make_frame(wire, true);
    std::uint32_t hresult = 1u;
    const std::uint64_t memory = wire.open_memory(readback_memory, heap_bytes);
    CHECK(memory != 0u);
    std::uint64_t placed = 9u;
    constexpr std::uint32_t refused_heap = 60u;
    CHECK(av_d3d12_CreateHeap12(wire, refused_heap, heap_of(heap_readback), 0u, heap_bytes, 0u, memory, 0u, &hresult,
                                &placed));
    CHECK_EQ(hresult, static_cast<std::uint32_t>(E_OUTOFMEMORY));
    CHECK_EQ(placed, std::uint64_t{0});
    CHECK(rig.sink.holding("over guest memory is refused") != 0u);
    make_triangle(wire, false, true);
    draw_triangle(wire);
    read_frame(wire);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    backend.shut_down();
    readback_native = nullptr;
    upload_native = nullptr;
}

TEST(d3d12_session_device, a_guest_that_keeps_asking_for_heaps_the_host_refuses_is_logged_a_few_times_and_then_rarely)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    if (!device12->exports_heaps()) return;
    setenv("AV_HEAP_PLACEMENT", "native", 1);
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12, nullptr, nullptr, source));
    unsetenv("AV_HEAP_PLACEMENT");
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    readback_native = nullptr;
    upload_native = nullptr;
    make_frame(wire, true);
    const std::uint64_t memory = wire.open_memory(readback_memory, heap_bytes);
    CHECK(memory != 0u);
    constexpr std::uint32_t asks = 300u;
    const std::uint32_t     before = rig.sink.lines();
    for (std::uint32_t ask = 0u; ask < asks; ask++) {
        std::uint32_t hresult = 1u;
        std::uint64_t placed = 9u;
        CHECK(av_d3d12_CreateHeap12(wire, 600u + ask, heap_of(heap_readback), 0u, heap_bytes, 0u, memory, 0u,
                                    &hresult, &placed));
        CHECK_EQ(hresult, static_cast<std::uint32_t>(E_OUTOFMEMORY));
    }
    const std::uint32_t told = rig.sink.lines() - before;
    CHECK(told >= av::LogCadence::spoken_first);
    CHECK(told <= av::LogCadence::spoken_first + asks / av::LogCadence::then_every);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    backend.shut_down();
    readback_native = nullptr;
    upload_native = nullptr;
}

namespace {

constexpr std::uint64_t arguments_at = 512u, counts_at = 768u, argument_bytes = 256u;
constexpr std::uint32_t draw_bytes = 16u;
constexpr IndirectArgument draw_argument = {0u, 0u, 0u, 0u};

void make_indirect(av::D3dLoopback &wire)
{
    make_frame(wire);
    make_triangle(wire, false);
    const std::uint32_t draw[4] = {3u, 1u, 0u, 0u};
    std::memcpy(wire.memory(heaps.upload, arguments_at), draw, sizeof draw);
    const std::uint32_t one = 1u;
    std::memcpy(wire.memory(heaps.upload, counts_at), &one, sizeof one);
    CHECK(av_d3d12_CreatePlacedResource12(wire, arguments, upload_heap, arguments_at, buffer_of(argument_bytes),
                                           state_generic_read, ClearValue{}, upload_address + arguments_at));
    CHECK(av_d3d12_CreatePlacedResource12(wire, counts, upload_heap, counts_at, buffer_of(argument_bytes),
                                           state_generic_read, ClearValue{}, upload_address + counts_at));
    CHECK(av_d3d12_CreateCommandSignature12(wire, signature, 0u, draw_bytes, 1u, &draw_argument));
}

using Misuse = void (*)(av::D3dLoopback &wire);

constexpr Misuse misuses[] = {
    [](av::D3dLoopback &wire) {
        const IndirectArgument taken[2] = {{5u, 0u, 0u, 1u}, draw_argument};
        CHECK(av_d3d12_CreateCommandSignature12(wire, signature + 1u, root, 20u, 2u, taken));
    },
    [](av::D3dLoopback &wire) {
        const IndirectArgument taken[2] = {{6u, 1u, 0u, 0u}, draw_argument};
        CHECK(av_d3d12_CreateCommandSignature12(wire, signature + 1u, root, 24u, 2u, taken));
    },
    [](av::D3dLoopback &wire) {
        const IndirectArgument taken[2] = {{6u, 0u, 0u, 0u}, draw_argument};
        CHECK(av_d3d12_CreateCommandSignature12(wire, signature + 1u, pipeline, 24u, 2u, taken));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, argument_bytes / draw_bytes + 1u, arguments, 0u,
                                          0u, 0u));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, arguments, 0u, counts, 2u));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, arguments, 0u, counts, argument_bytes));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, arguments, 0u, pipeline, 0u));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, target, 0u, 0u, 0u));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, arguments, 0u, target, 0u));
    },
    [](av::D3dLoopback &wire) {
        CHECK(av_d3d12_ExecuteIndirect12(wire, list, pipeline, 1u, arguments, 0u, 0u, 0u));
    },
};

}

TEST(d3d12_session_device, a_triangle_drawn_indirectly_reads_back_where_it_was_drawn)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    make_indirect(wire);
    bind_triangle(wire);
    CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 4u, arguments, 0u, counts, 0u));
    read_frame(wire);
    judge_triangle(wire);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

namespace {

constexpr IndirectArgument vertex_view_argument = {3u, 0u, 0u, 0u};
constexpr std::uint32_t view_command_bytes = 32u;

void indirect_through_a_vertex_view(av::D3dLoopback &wire, std::uint64_t view_address, bool &translated)
{
    make_frame(wire);
    make_triangle(wire, false);
    struct Command {
        std::uint64_t address;
        std::uint32_t size, stride;
        std::uint32_t draw[4];
    } command = {view_address, 3u * sizeof(Vertex), sizeof(Vertex), {3u, 1u, 0u, 0u}};
    std::memcpy(wire.memory(heaps.upload, arguments_at), &command, sizeof command);
    CHECK(av_d3d12_CreatePlacedResource12(wire, arguments, upload_heap, arguments_at, buffer_of(argument_bytes),
                                           state_generic_read, ClearValue{}, upload_address + arguments_at));
    const IndirectArgument taken[2] = {vertex_view_argument, draw_argument};
    translated = av_d3d12_CreateCommandSignature12(wire, signature, 0u, view_command_bytes, 2u, taken);
}

}

TEST(d3d12_session_device, a_vertex_buffer_view_in_an_argument_buffer_reaches_the_host_translated)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire) || !device12->translates_indirect_addresses()) {
        std::fprintf(stderr, "this vkd3d-proton translates no indirect addresses: nothing judged\n");
        backend.shut_down();
        return;
    }
    bool made = false;
    indirect_through_a_vertex_view(wire, upload_address, made);
    CHECK(made);
    bind_triangle(wire);
    const VertexBufferView emptied = {upload_address, 0u, sizeof(Vertex)};
    CHECK(av_d3d12_SetVertexBuffers12(wire, list, 0u, 1u, &emptied));
    CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, arguments, 0u, 0u, 0u));
    read_frame(wire);
    judge_triangle(wire);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    backend.shut_down();
}

TEST(d3d12_session_device, a_vertex_buffer_view_naming_no_buffer_draws_nothing_and_leaves_the_device_healthy)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire) || !device12->translates_indirect_addresses()) {
        backend.shut_down();
        return;
    }
    bool made = false;
    indirect_through_a_vertex_view(wire, 0x7ead00000000ull, made);
    CHECK(made);
    bind_triangle(wire);
    CHECK(av_d3d12_ExecuteIndirect12(wire, list, signature, 1u, arguments, 0u, 0u, 0u));
    read_frame(wire);
    CHECK_EQ(pixel(wire, 4u, size / 2u), 0xff0000ffu);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    backend.shut_down();
}

TEST(d3d12_session_device, each_indirect_misuse_removes_a_healthy_device)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    for (const Misuse misuse : misuses) {
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        if (!imports(*device12, wire)) {
            backend.shut_down();
            return;
        }
        make_indirect(wire);
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, 0u);
        misuse(wire);
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, driver_internal_error);
        backend.shut_down();
    }
}

namespace {

constexpr std::uint32_t named_heap = 40u;

using HeapMisuse = void (*)(av::D3dLoopback &wire);

std::uint32_t answered = 0u;

const HeapMisuse heap_misuses[] = {
    [](av::D3dLoopback &wire) {
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, 0x1234u, 0u, &answered));
    },
    [](av::D3dLoopback &wire) {
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(heap_readback), 0u, heap_bytes, 0u, 0u, 0u, &answered));
    },
    [](av::D3dLoopback &wire) {
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(heap_readback), 0u, heap_bytes + 4096u, 0u,
                                    heaps.readback, 0u, &answered));
    },
    [](av::D3dLoopback &wire) {
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(0u), 0u, heap_bytes, 0u, heaps.readback, 0u, &answered));
    },
    [](av::D3dLoopback &wire) {
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(5u), 0u, heap_bytes, 0u, heaps.readback, 0u, &answered));
    },
    [](av::D3dLoopback &wire) {
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(heap_readback), 0u, 0u, 0u, heaps.readback, 0u, &answered));
    },
    [](av::D3dLoopback &wire) {
        const std::uint64_t gone = wire.open_memory(upload_memory, heap_bytes);
        CHECK(gone != 0u);
        wire.close_memory(gone);
        static_cast<void>(av_d3d12_CreateHeap12(wire, named_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, gone, 0u, &answered));
    },
};

}

TEST(d3d12_session_device, each_cpu_visible_heap_over_memory_it_cannot_have_removes_a_healthy_device)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    for (const HeapMisuse misuse : heap_misuses) {
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        if (!imports(*device12, wire)) {
            backend.shut_down();
            return;
        }
        make_frame(wire);
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, 0u);
        misuse(wire);
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, driver_internal_error);
        backend.shut_down();
    }
}

TEST(d3d12_session_device, a_heap_over_memory_vkd3d_cannot_open_answers_out_of_memory_and_keeps_the_device)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    alignas(4096) static std::uint8_t bytes[heap_bytes];
    av::LocalImport unexported(bytes, sizeof bytes, 99u);
    unexported.memory().bind(*source);
    std::uint64_t unexported_handle = 0u;
    av::ImportedBlob *taken = nullptr;
    CHECK_STATUS(rig.imports.take(99u, unexported.memory(), taken), av::Status::ok);
    CHECK(backend.adopt(unexported.memory(), av::BlobRole(), unexported_handle));
    CHECK_STATUS(rig.imports.name(*taken, unexported_handle), av::Status::ok);
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    hresult = 1u;
    CHECK(av_d3d12_CreateHeap12(wire, named_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, unexported_handle, 0u,
                                &hresult));
    CHECK_EQ(hresult, static_cast<std::uint32_t>(E_OUTOFMEMORY));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    backend.abandon(unexported.memory(), unexported_handle);
    CHECK(rig.imports.forget(99u) == &unexported.memory());
    backend.shut_down();
}

TEST(d3d12_session_device, a_heap_is_its_own_memory_and_outlives_the_guest_s_hold_on_it)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    make_frame(wire);
    const std::uint64_t memory = wire.open_memory(upload_memory, 2u * heap_bytes);
    CHECK(memory != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, memory, 0u));
    CHECK(av_d3d12_CreatePlacedResource12(wire, vertices, upload_heap, 0u, buffer_of(256u), state_generic_read,
                                           ClearValue{}, upload_address));
    std::uint32_t words[64];
    for (std::uint32_t at = 0u; at < 64u; at++) words[at] = 0xa000u + at;
    std::memcpy(wire.memory(memory), words, sizeof words);
    wire.close_memory(memory);
    CHECK(wire.memory(memory) == nullptr);
    CHECK(av_d3d12_CopyBufferRegion12(wire, list, readback, 0u, vertices, 0u, sizeof words));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    std::uint32_t drained = 1u;
    CHECK(av_d3d12_Finish12(wire, &drained));
    CHECK_EQ(drained, 0u);
    CHECK_EQ(std::memcmp(wire.memory(heaps.readback), words, sizeof words), 0);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

TEST(d3d12_session_device, a_sampler_heap_past_2048_is_made_with_2048)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    constexpr std::uint32_t descriptors_sampler = 1u, shader_visible = 1u, reported = 4000u, made = 2048u;
    const std::uint32_t indices[2] = {made - 1u, made};
    for (const std::uint32_t index : indices) {
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        std::uint32_t hresult = 1u;
        CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
        CHECK(av_d3d12_CreateDescriptorHeap12(wire, rtv_heap, descriptors_sampler, reported, shader_visible));
        SamplerDesc sampler = {};
        sampler.filter = 0u;
        sampler.address_u = sampler.address_v = sampler.address_w = 1u;
        sampler.comparison = 1u;
        sampler.max_lod = 1000.0f;
        CHECK(av_d3d12_CreateSampler12(wire, DescriptorRef{rtv_heap, index}, sampler));
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, index < made ? 0u : driver_internal_error);
        backend.shut_down();
    }
}

TEST(d3d12_session_device, a_buffer_placed_inside_a_spanning_buffer_is_taken_and_a_misplaced_one_refused)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    constexpr std::uint64_t heap_size = 1u << 20u, piece = 64u * 1024u, base = 0x300000000ull;
    constexpr std::uint32_t heap = 1u, spanning = 2u, inner = 3u, misplaced = 4u;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK(av_d3d12_CreateHeap12(wire, heap, heap_of(heap_default), 0u, heap_size, piece, 0u, 0u));
    CHECK(av_d3d12_CreatePlacedResource12(wire, spanning, heap, 0u, buffer_of(heap_size), 0u, ClearValue{}, base));
    CHECK(av_d3d12_CreatePlacedResource12(wire, inner, heap, piece, buffer_of(piece), 0u, ClearValue{}, base + piece));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK(av_d3d12_CreatePlacedResource12(wire, misplaced, heap, 2u * piece, buffer_of(piece), 0u, ClearValue{},
                                           base + piece));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, driver_internal_error);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

TEST(d3d12_session_device, a_placed_resource_past_its_heap_of_a_denied_kind_or_an_id_in_use_is_refused)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    constexpr std::uint64_t heap_size = 1u << 20u, piece = 64u * 1024u, base = 0x300000000ull;
    constexpr std::uint32_t heap = 1u, first = 2u, deny_non_rt_ds_textures = 0x80u;
    ResourceDesc texture = {};
    texture.width = 64u;
    texture.dimension = dimension_texture2d;
    texture.height = 64u;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_b8g8r8a8;
    texture.sample_count = 1u;
    for (std::uint32_t refusal = 0u; refusal < 4u; refusal++) {
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        std::uint32_t hresult = 1u;
        CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
        CHECK(av_d3d12_CreateHeap12(wire, heap, heap_of(heap_default), refusal == 1u ? deny_non_rt_ds_textures : 0u,
                                    heap_size, piece, 0u, 0u));
        CHECK(av_d3d12_CreatePlacedResource12(wire, first, heap, 0u, buffer_of(piece), 0u, ClearValue{}, base));
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, 0u);
        if (refusal == 0u)
            CHECK(av_d3d12_CreatePlacedResource12(wire, 3u, heap, heap_size - piece, buffer_of(2u * piece), 0u,
                                                   ClearValue{}, base + heap_size - piece));
        else if (refusal == 1u)
            CHECK(av_d3d12_CreatePlacedResource12(wire, 3u, heap, piece, texture, 0u, ClearValue{}, 0u));
        else if (refusal == 2u)
            CHECK(av_d3d12_CreatePlacedResource12(wire, first, heap, piece, texture, 0u, ClearValue{}, 0u));
        else
            CHECK(av_d3d12_CreatePlacedResource12(wire, first, heap, piece, buffer_of(piece), 0u, ClearValue{},
                                                   base + piece));
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, driver_internal_error);
        CHECK(backend.invariants_hold());
        backend.shut_down();
    }
}

namespace {

constexpr std::uint32_t heap_flag_shared = 0x1u;
constexpr std::uint64_t shown_key = 0x5eed00000000d312ull;
constexpr std::uint32_t opaque_red = 0xffff0000u;
constexpr std::uint32_t opaque_green = 0xff00ff00u;

void make_shown(av::D3dLoopback &wire)
{
    make_frame(wire);
    ResourceDesc texture = {};
    texture.width = size;
    texture.dimension = dimension_texture2d;
    texture.height = size;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_b8g8r8a8;
    texture.sample_count = 1u;
    texture.flags = flag_render_target;
    CHECK(av_d3d12_CreateCommittedResource12(wire, shown, heap_of(heap_default), heap_flag_shared, texture,
                                              state_render_target, ClearValue{}, 0u));
    CHECK(av_d3d12_CreateDescriptorHeap12(wire, shown_rtv, descriptors_rtv, 1u, 0u));
    ViewDesc view = {};
    view.resource = shown;
    CHECK(av_d3d12_CreateRenderTargetView12(wire, DescriptorRef{shown_rtv, 0u}, 0u, view));
    CHECK(av_d3d12_ShareResource12(wire, shown, shown_key));
}

void draw_cleared(av::D3dLoopback &wire, Float4 colour)
{
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{shown_rtv, 0u}, colour, 0u, nullptr));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    CHECK(av_d3d12_ResetCommandAllocator12(wire, allocator));
    CHECK(av_d3d12_ResetCommandList12(wire, list, allocator, 0u));
}

void present_cleared(av::D3dLoopback &wire, Float4 colour, std::uint32_t point)
{
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{shown_rtv, 0u}, colour, 0u, nullptr));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    CHECK(av_d3d12_PresentResource12(wire, queue, shown, point));
    CHECK(av_d3d12_ResetCommandAllocator12(wire, allocator));
    CHECK(av_d3d12_ResetCommandList12(wire, list, allocator, 0u));
}

bool frame_everywhere(const AvFrameView &view, std::uint32_t expected)
{
    for (std::uint32_t y = 0u; y < view.height; y++)
        for (std::uint32_t x = 0u; x < view.width; x++) {
            std::uint32_t value = 0u;
            std::memcpy(&value, view.pixels + y * view.row_pitch + x * 4u, sizeof value);
            if (value != expected) return false;
        }
    return view.width == size && view.height == size;
}

bool shown_everywhere(av::D3d11HostDevice &device11, std::uint32_t expected)
{
    std::uint32_t pixels[size * size] = {};
    if (!device11.read_image(1u, shown_key, pixels, row_pitch, size, size)) return false;
    for (const std::uint32_t value : pixels)
        if (value != expected) return false;
    return true;
}

using ShowMisuse = void (*)(av::D3dLoopback &);

const ShowMisuse show_misuses[] = {
    [](av::D3dLoopback &wire) { CHECK(av_d3d12_ShareResource12(wire, target, shown_key + 1u)); },
    [](av::D3dLoopback &wire) { CHECK(av_d3d12_ShareResource12(wire, shown, 0u)); },
    [](av::D3dLoopback &wire) { CHECK(av_d3d12_ShareResource12(wire, 99u, shown_key + 1u)); },
    [](av::D3dLoopback &wire) { CHECK(av_d3d12_ShareResource12(wire, shown, shown_key)); },
    [](av::D3dLoopback &wire) { CHECK(av_d3d12_PresentResource12(wire, 99u, shown, 1u)); },
    [](av::D3dLoopback &wire) { CHECK(av_d3d12_PresentResource12(wire, queue, 99u, 1u)); },
};

}

TEST(d3d12_session_device, a_shared_buffer_drawn_on_d3d12_is_what_d3d11_reads_under_its_key)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    make_shown(wire);
    present_cleared(wire, Float4{{1.0f, 0.0f, 0.0f, 1.0f}}, 1u);
    CHECK(shown_everywhere(*device11, opaque_red));
    present_cleared(wire, Float4{{0.0f, 1.0f, 0.0f, 1.0f}}, 2u);
    CHECK(shown_everywhere(*device11, opaque_green));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
    shares->release(1u, shown_key);
    device11->retire_shares();
    CHECK(!shown_everywhere(*device11, opaque_green));
}

TEST(d3d12_session_device, a_buffer_a_blt_read_is_staged_at_each_present_in_its_queue_s_order)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::HostHeap       surfaces_heap;
    av::SharedSurfaces surfaces(surfaces_heap);
    av::D3dBackend     backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12, &surfaces));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    make_shown(wire);
    present_cleared(wire, Float4{{1.0f, 0.0f, 0.0f, 1.0f}}, 1u);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    AvFrameView view = {};
    CHECK(!surfaces.map(av::SurfacePoint(shown_key, 1u), 1000u, view));
    present_cleared(wire, Float4{{0.0f, 1.0f, 0.0f, 1.0f}}, 2u);
    draw_cleared(wire, Float4{{0.0f, 0.0f, 1.0f, 1.0f}});
    CHECK(surfaces.map(av::SurfacePoint(shown_key, 2u), 1000u, view));
    CHECK(frame_everywhere(view, opaque_green));
    surfaces.unmap(av::SurfacePoint(shown_key, 2u));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(surfaces.invariants_hold());
    backend.shut_down();
    surfaces.release(0u);
    shares->release(1u, shown_key);
    device11->retire_shares();
}

TEST(d3d12_session_device, a_heap_and_a_committed_buffer_shared_in_one_session_open_in_another_under_their_key)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::D3dBackend other(rig.log);
    av::SessionShares session;
    av::SessionShares twin;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    CHECK(other.initialize(*shared_as(twin, shares, other), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    av::D3dLoopback second(other, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire) || !imports(*device12, second)) {
        backend.shut_down();
        other.shut_down();
        return;
    }
    constexpr std::uint64_t key_buffer = 0x5eed00000000b0f0ull;
    constexpr std::uint64_t key_heap = 0x5eed00000000b0f1ull;
    constexpr std::uint32_t shared_buffer = 40u;
    constexpr std::uint32_t shared_heap = 41u;
    constexpr std::uint32_t opened_buffer = 50u;
    constexpr std::uint32_t opened_heap = 51u;
    constexpr std::uint64_t bytes = 65536u;
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(second, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    ResourceDesc desc = {};
    desc.width = bytes;
    desc.dimension = dimension_buffer;
    desc.height = 1u;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.sample_count = 1u;
    desc.layout = 1u;
    hresult = 1u;
    CHECK(av_d3d12_CreateCommittedResource12(wire, shared_buffer, heap_of(heap_default), heap_flag_shared, desc,
                                              state_copy_dest, ClearValue{}, 0x700000000ull, &hresult));
    CHECK_EQ(hresult, 0u);
    hresult = 1u;
    CHECK(av_d3d12_CreateHeap12(wire, shared_heap, heap_of(heap_default), heap_flag_shared, bytes, 65536u, 0u, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_ShareObject12(wire, shared_buffer, key_buffer));
    CHECK(av_d3d12_ShareObject12(wire, shared_heap, key_heap));
    std::uint32_t kind = 9u, dimension = 0u, height = 0u, depth = 0u, mips = 0u, format = 0u, samples = 0u, flags = 0u;
    std::uint64_t opened_width = 0u, opened_size = 0u;
    CHECK(av_d3d12_OpenObject12(second, opened_buffer, key_buffer, 0x800000000ull, &kind, &dimension, &height, &depth,
                                &mips, &format, &samples, &flags, &opened_width, &opened_size));
    CHECK_EQ(kind, 1u);
    CHECK_EQ(dimension, dimension_buffer);
    CHECK_EQ(opened_width, bytes);
    CHECK(av_d3d12_OpenObject12(second, opened_heap, key_heap, 0u, &kind, &dimension, &height, &depth, &mips, &format,
                                &samples, &flags, &opened_width, &opened_size));
    CHECK_EQ(kind, 2u);
    CHECK_EQ(opened_size, bytes);
    CHECK(av_d3d12_OpenObject12(second, 60u, key_heap + 7u, 0u, &kind, &dimension, &height, &depth, &mips, &format,
                                &samples, &flags, &opened_width, &opened_size));
    CHECK_EQ(kind, 0u);
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(second, &removed));
    CHECK_EQ(removed, 0u);
    other.shut_down();
    backend.shut_down();
    shares->release(1u, key_buffer);
    shares->release(1u, key_heap);
    device11->retire_shares();
}

TEST(d3d12_session_device, a_texture_direct3d11_shares_opens_in_direct3d12_over_the_same_memory)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend    producer(rig.log);
    av::D3dBackend    consumer(rig.log);
    av::SessionShares produced;
    av::SessionShares consumed;
    CHECK(producer.initialize(*shared_as(produced, shares, producer), rig.imports, device11, device12));
    CHECK(consumer.initialize(*shared_as(consumed, shares, consumer), rig.imports, device11, device12));
    av::D3dLoopback eleven(producer, rig.imports, source, region_bytes, lent_bytes);
    av::D3dLoopback twelve(consumer, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, twelve)) {
        producer.shut_down();
        consumer.shut_down();
        return;
    }
    constexpr std::uint64_t key = 0x5eed0000000011d2ull;
    constexpr std::uint32_t texture11 = 70u;
    constexpr std::uint32_t opened = 71u;
    std::uint32_t hresult = 1u, level = 0u;
    CHECK(av::d3dproto::av_d3d_CreateDevice11(eleven, 1u, 0u, 0u, &hresult, &level));
    CHECK_EQ(hresult, 0u);
    av::d3dproto::ResourceDesc shared = {};
    shared.dimension = dimension_texture2d;
    shared.width = size;
    shared.height = size;
    shared.depth_or_array = 1u;
    shared.mip_levels = 1u;
    shared.format = format_b8g8r8a8;
    shared.sample_count = 1u;
    shared.bind_flags = 0x8u | 0x20u;
    shared.misc_flags = 0x2u;
    CHECK(av::d3dproto::av_d3d_CreateResource(eleven, texture11, shared, key, 0u, 0u));
    static std::uint32_t pattern[size * size];
    for (std::uint32_t at = 0u; at < size * size; at++) pattern[at] = 0xff000000u | (at * 2654435761u >> 8u);
    const av::d3dproto::Box whole = {0u, 0u, 0u, size, size, 1u};
    CHECK(av::d3dproto::av_d3d_UpdateSubresource(eleven, texture11, 0u, 1u, whole, row_pitch, 0u, 0u, pattern,
                                                 sizeof pattern));
    CHECK(av::d3dproto::av_d3d_Finish11(eleven, &hresult));
    CHECK_EQ(hresult, 0u);
    producer.end_batch();
    std::uint64_t marked = 0u, reached = 0u;
    for (std::uint32_t polls = 0u; polls < 5000u; polls++) {
        static_cast<void>(producer.poll());
        producer.gpu_progress(marked, reached);
        if (reached >= marked) break;
        usleep(1000u);
    }
    CHECK(reached >= marked);

    make_frame(twelve);
    std::uint32_t kind = 9u, dimension = 0u, height = 0u, depth = 0u, mips = 0u, format = 0u, samples = 0u, flags = 0u;
    std::uint64_t width = 0u, bytes = 0u;
    CHECK(av_d3d12_OpenObject12(twelve, opened, key, 0u, &kind, &dimension, &height, &depth, &mips, &format, &samples,
                                &flags, &width, &bytes));
    CHECK_EQ(kind, 1u);
    CHECK_EQ(dimension, dimension_texture2d);
    CHECK_EQ(width, std::uint64_t{size});
    CHECK_EQ(height, size);
    CHECK_EQ(format, format_b8g8r8a8);
    CHECK((flags & flag_render_target) != 0u);
    const Barrier to_copy = {0u, 0u, opened, 0xffffffffu, 0u, state_copy_source, 0u, 0u};
    CHECK(av_d3d12_ResourceBarrier12(twelve, list, 1u, &to_copy));
    CopyLocation into = {};
    into.resource = readback;
    into.type = 1u;
    into.format = format_b8g8r8a8;
    into.width = size;
    into.height = size;
    into.depth = 1u;
    into.row_pitch = row_pitch;
    CopyLocation from = {};
    from.resource = opened;
    CHECK(av_d3d12_CopyTextureRegion12(twelve, list, into, 0u, 0u, 0u, from, 0u, Box{}));
    CHECK(av_d3d12_CloseCommandList12(twelve, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(twelve, queue, 1u, lists));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(twelve, &removed));
    CHECK_EQ(removed, 0u);
    std::uint32_t wrong = 0u;
    for (std::uint32_t y = 0u; y < size; y++)
        for (std::uint32_t x = 0u; x < size; x++)
            if (pixel(twelve, x, y) != pattern[y * size + x]) wrong++;
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(twelve.refused(), 0u);
    CHECK(av_d3d12_OpenObject12(twelve, opened + 1u, key, 0u, &kind, &dimension, &height, &depth, &mips, &format,
                                &samples, &flags, &width, &bytes));
    CHECK_EQ(kind, 1u);
    CHECK(av_d3d12_Finish12(twelve, &removed));
    CHECK_EQ(removed, 0u);
    consumer.shut_down();
    producer.shut_down();
    shares->release(1u, key);
    device11->retire_shares();
}

TEST(d3d12_session_device, rows_another_adapter_lends_become_an_image_whose_copies_land_in_the_lent_pages)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend    backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    make_frame(wire);
    constexpr std::uint32_t lent_image = 80u;
    constexpr std::uint32_t lent_pitch = 512u;
    const std::uint64_t     lent = wire.open_memory(readback_memory, std::uint64_t{lent_pitch} * size);
    CHECK(lent != 0u);
    std::memset(wire.memory(lent), 0xcd, std::size_t{lent_pitch} * size);
    ResourceDesc desc = {};
    desc.dimension = dimension_texture2d;
    desc.width = size;
    desc.height = size;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.format = format_b8g8r8a8;
    desc.sample_count = 1u;
    desc.layout = layout_row_major;
    desc.flags = flag_render_target;
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_AdoptLentImage12(wire, lent_image, lent, desc, lent_pitch, &hresult));
    CHECK_EQ(hresult, 0u);
    std::uint32_t refused = 1u;
    CHECK(av_d3d12_AdoptLentImage12(wire, lent_image + 1u, lent, desc, size * 2u, &refused));
    CHECK_EQ(refused, 0x80070057u);
    CHECK(av_d3d12_ClearRenderTargetView12(wire, list, DescriptorRef{rtv_heap, 0u}, Float4{{0.0f, 0.0f, 1.0f, 1.0f}},
                                            0u, nullptr));
    const Barrier into_copy[2] = {{0u, 0u, target, 0xffffffffu, state_render_target, state_copy_source, 0u, 0u},
                                  {0u, 0u, lent_image, 0xffffffffu, 0u, state_copy_dest, 0u, 0u}};
    CHECK(av_d3d12_ResourceBarrier12(wire, list, 2u, into_copy));
    CHECK(av_d3d12_CopyResource12(wire, list, lent_image, target));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    std::uint32_t wrong = 0u;
    for (std::uint32_t y = 0u; y < size; y++)
        for (std::uint32_t x = 0u; x < size; x++) {
            std::uint32_t value = 0u;
            std::memcpy(&value, wire.memory(lent, std::uint64_t{y} * lent_pitch + x * 4u), sizeof value);
            if (value != 0xff0000ffu) wrong++;
        }
    CHECK_EQ(wrong, 0u);
    std::uint32_t tail = 0u;
    std::memcpy(&tail, wire.memory(lent, size * 4u), sizeof tail);
    CHECK_EQ(tail, 0xcdcdcdcdu);
    CHECK(av_d3d12_CreateCommittedResource12(wire, 81u, heap_of(heap_default), 0u, buffer_of(heap_bytes),
                                              state_copy_dest, ClearValue{}, 0x900000000ull));
    CHECK(av_d3d12_Destroy12(wire, lent_image));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    wire.close_memory(lent);
    backend.shut_down();
}

TEST(d3d12_session_device, a_copy_queue_moves_and_fills_rows_another_adapter_lends_by_layout_barrier_and_copy)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend    backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    make_frame(wire);
    constexpr std::uint32_t list_copy = 3u;
    constexpr std::uint32_t copy_queue = 90u;
    constexpr std::uint32_t copy_allocator = 91u;
    constexpr std::uint32_t copy_list = 92u;
    constexpr std::uint32_t pixels = 93u;
    constexpr std::uint32_t lent_image = 94u;
    constexpr std::uint32_t lent_pitch = 512u;
    constexpr std::uint32_t sync_copy = 0x200u;
    constexpr std::uint32_t access_copy_dest = 0x400u;
    constexpr std::uint32_t access_none = 0x80000000u;
    constexpr std::uint32_t layout_common = 0u;
    constexpr std::uint32_t green = 0xff00ff00u;
    heaps.upload = wire.open_memory(upload_memory, heap_bytes);
    CHECK(heaps.upload != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, heaps.upload, 0u));
    for (std::uint32_t at = 0u; at < size * size; at++) std::memcpy(wire.memory(heaps.upload, at * 4u), &green, 4u);
    CHECK(av_d3d12_CreatePlacedResource12(wire, pixels, upload_heap, 0u, buffer_of(row_pitch * size),
                                           state_generic_read, ClearValue{}, upload_address));
    const std::uint64_t lent = wire.open_memory(readback_memory, std::uint64_t{lent_pitch} * size);
    CHECK(lent != 0u);
    std::memset(wire.memory(lent), 0xcd, std::size_t{lent_pitch} * size);
    ResourceDesc desc = {};
    desc.dimension = dimension_texture2d;
    desc.width = size;
    desc.height = size;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.format = format_b8g8r8a8;
    desc.sample_count = 1u;
    desc.layout = layout_row_major;
    desc.flags = flag_render_target;
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_AdoptLentImage12(wire, lent_image, lent, desc, lent_pitch, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateCommandQueue12(wire, copy_queue, list_copy, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, copy_allocator, list_copy));
    CHECK(av_d3d12_CreateCommandList12(wire, copy_list, copy_allocator, list_copy, 0u));
    LayoutBarrier into = {};
    into.type = 1u;
    into.sync_before = 0u;
    into.sync_after = sync_copy;
    into.access_before = access_none;
    into.access_after = access_copy_dest;
    into.layout_before = layout_common;
    into.layout_after = layout_common;
    into.resource = lent_image;
    into.mip_count = 1u;
    into.slice_count = 1u;
    into.plane_count = 1u;
    CHECK(av_d3d12_Barrier12(wire, copy_list, 1u, &into));
    CopyLocation to = {};
    to.resource = lent_image;
    CopyLocation from = {};
    from.resource = pixels;
    from.type = 1u;
    from.format = format_b8g8r8a8;
    from.width = size;
    from.height = size;
    from.depth = 1u;
    from.row_pitch = row_pitch;
    CHECK(av_d3d12_CopyTextureRegion12(wire, copy_list, to, 0u, 0u, 0u, from, 0u, Box{}));
    CHECK(av_d3d12_CloseCommandList12(wire, copy_list));
    const std::uint32_t lists[1] = {copy_list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, copy_queue, 1u, lists));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    std::uint32_t wrong = 0u;
    for (std::uint32_t y = 0u; y < size; y++)
        for (std::uint32_t x = 0u; x < size; x++) {
            std::uint32_t value = 0u;
            std::memcpy(&value, wire.memory(lent, std::uint64_t{y} * lent_pitch + x * 4u), sizeof value);
            if (value != green) wrong++;
        }
    CHECK_EQ(wrong, 0u);
    CHECK(av_d3d12_Destroy12(wire, lent_image));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
    wire.close_memory(lent);
    backend.shut_down();
}

namespace {

enum class Holder : std::uint32_t { committed, placed, lent, guest_pages };

constexpr std::uint32_t queue_types[] = {0u, 2u, 3u};
constexpr Holder        holders[] = {Holder::committed, Holder::placed, Holder::lent, Holder::guest_pages};

LayoutBarrier copy_barrier(std::uint32_t resource, std::uint32_t sync_before, std::uint32_t access_before,
                           std::uint32_t access_after)
{
    constexpr std::uint32_t sync_copy = 0x200u;
    LayoutBarrier barrier = {};
    barrier.type = 1u;
    barrier.sync_before = sync_before;
    barrier.sync_after = sync_copy;
    barrier.access_before = access_before;
    barrier.access_after = access_after;
    barrier.resource = resource;
    barrier.mip_count = 1u;
    barrier.slice_count = 1u;
    barrier.plane_count = 1u;
    return barrier;
}

ResourceDesc texture_of(bool row_major)
{
    ResourceDesc desc = {};
    desc.dimension = dimension_texture2d;
    desc.width = size;
    desc.height = size;
    desc.depth_or_array = 1u;
    desc.mip_levels = 1u;
    desc.format = format_b8g8r8a8;
    desc.sample_count = 1u;
    desc.layout = row_major ? layout_row_major : 0u;
    desc.flags = flag_render_target;
    return desc;
}

CopyLocation rows_in(std::uint32_t buffer)
{
    CopyLocation rows = {};
    rows.resource = buffer;
    rows.type = 1u;
    rows.format = format_b8g8r8a8;
    rows.width = size;
    rows.height = size;
    rows.depth = 1u;
    rows.row_pitch = row_pitch;
    return rows;
}

bool make_holder(av::D3dLoopback &wire, Holder holder, std::uint32_t id, std::uint64_t &lent)
{
    std::uint32_t hresult = 1u;
    std::uint64_t placed = 0u;
    switch (holder) {
    case Holder::committed:
        return av_d3d12_CreateCommittedResource12(wire, id, heap_of(heap_default), 0u, texture_of(false), 0u,
                                                   ClearValue{}, 0u, &hresult) &&
               hresult == 0u;
    case Holder::placed:
        return av_d3d12_CreateHeap12(wire, id + 1u, heap_of(heap_default), 0u, heap_bytes, 0u, 0u, 0u, &hresult,
                                     &placed) &&
               hresult == 0u &&
               av_d3d12_CreatePlacedResource12(wire, id, id + 1u, 0u, texture_of(false), 0u, ClearValue{}, 0u);
    case Holder::lent:
        lent = wire.open_memory(readback_memory, std::uint64_t{row_pitch} * size);
        return lent != 0u && av_d3d12_AdoptLentImage12(wire, id, lent, texture_of(true), row_pitch, &hresult) &&
               hresult == 0u;
    case Holder::guest_pages:
        lent = wire.open_memory(readback_memory, heap_bytes);
        return lent != 0u &&
               av_d3d12_CreateHeap12(wire, id + 1u, heap_of(heap_readback), 0u, heap_bytes, 0u, lent, 0u) &&
               av_d3d12_CreatePlacedResource12(wire, id, id + 1u, 0u, buffer_of(row_pitch * size), state_copy_dest,
                                               ClearValue{}, readback_address + heap_bytes);
    }
    return false;
}

std::uint32_t wrong_pixels(const av::D3dLoopback &wire, std::uint32_t expected)
{
    std::uint32_t wrong = 0u;
    for (std::uint32_t y = 0u; y < size; y++)
        for (std::uint32_t x = 0u; x < size; x++) wrong += pixel(wire, x, y) != expected ? 1u : 0u;
    return wrong;
}

void copy_through(av::D3dLoopback &wire, std::uint32_t type, Holder holder, std::uint32_t base, std::uint32_t colour)
{
    constexpr std::uint32_t access_copy_dest = 0x400u;
    constexpr std::uint32_t access_copy_source = 0x800u;
    constexpr std::uint32_t access_none = 0x80000000u;
    constexpr std::uint32_t sync_copy = 0x200u;
    const std::uint32_t command_queue = base, command_allocator = base + 1u, command_list = base + 2u;
    const std::uint32_t pixels = base + 3u, held = base + 4u;
    for (std::uint32_t at = 0u; at < size * size; at++) std::memcpy(wire.memory(heaps.upload, at * 4u), &colour, 4u);
    std::memset(wire.memory(heaps.readback), 0xcd, heap_bytes);
    CHECK(av_d3d12_CreatePlacedResource12(wire, pixels, upload_heap, 0u, buffer_of(row_pitch * size),
                                           state_generic_read, ClearValue{}, upload_address));
    std::uint64_t lent = 0u;
    CHECK(make_holder(wire, holder, held, lent));
    CHECK(av_d3d12_CreateCommandQueue12(wire, command_queue, type, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, command_allocator, type));
    CHECK(av_d3d12_CreateCommandList12(wire, command_list, command_allocator, type, 0u));
    if (holder == Holder::guest_pages) {
        CHECK(av_d3d12_CopyBufferRegion12(wire, command_list, held, 0u, pixels, 0u, row_pitch * size));
        LayoutBarrier between = {};
        between.sync_before = sync_copy;
        between.sync_after = sync_copy;
        between.access_before = access_copy_dest;
        between.access_after = access_copy_source;
        CHECK(av_d3d12_Barrier12(wire, command_list, 1u, &between));
        CHECK(av_d3d12_CopyBufferRegion12(wire, command_list, readback, 0u, held, 0u, row_pitch * size));
    } else {
        const LayoutBarrier into = copy_barrier(held, 0u, access_none, access_copy_dest);
        CHECK(av_d3d12_Barrier12(wire, command_list, 1u, &into));
        CopyLocation texture = {};
        texture.resource = held;
        CHECK(av_d3d12_CopyTextureRegion12(wire, command_list, texture, 0u, 0u, 0u, rows_in(pixels), 0u, Box{}));
        const LayoutBarrier out = copy_barrier(held, sync_copy, access_copy_dest, access_copy_source);
        CHECK(av_d3d12_Barrier12(wire, command_list, 1u, &out));
        CHECK(av_d3d12_CopyTextureRegion12(wire, command_list, rows_in(readback), 0u, 0u, 0u, texture, 0u, Box{}));
    }
    CHECK(av_d3d12_CloseCommandList12(wire, command_list));
    const std::uint32_t lists[1] = {command_list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, command_queue, 1u, lists));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    const std::uint32_t wrong = wrong_pixels(wire, colour);
    if (wrong != 0u) std::fprintf(stderr, "queue type %u holder %u: %u pixels wrong\n", type, static_cast<std::uint32_t>(holder), wrong);
    CHECK_EQ(wrong, 0u);
    for (const std::uint32_t id : {command_list, command_allocator, command_queue, held, pixels})
        CHECK(av_d3d12_Destroy12(wire, id));
    if (holder == Holder::placed || holder == Holder::guest_pages) CHECK(av_d3d12_Destroy12(wire, held + 1u));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    if (lent != 0u) wire.close_memory(lent);
}

}

TEST(d3d12_session_device, each_kind_of_resource_the_host_holds_is_copied_through_barriers_on_direct_compute_and_copy_queues)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend    backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    make_frame(wire);
    heaps.upload = wire.open_memory(upload_memory, heap_bytes);
    CHECK(heaps.upload != 0u);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_upload), 0u, heap_bytes, 0u, heaps.upload, 0u));
    std::uint32_t base = 200u;
    for (const std::uint32_t type : queue_types)
        for (const Holder holder : holders) {
            copy_through(wire, type, holder, base, 0xff000000u | (base * 0x010307u));
            base += 10u;
        }
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

TEST(d3d12_session_device, each_share_or_present_misuse_removes_a_healthy_device)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    for (const ShowMisuse misuse : show_misuses) {
        av::D3dBackend backend(rig.log);
        av::SessionShares session;
        CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
        av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
        make_shown(wire);
        std::uint32_t removed = 1u;
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, 0u);
        misuse(wire);
        CHECK(av_d3d12_Finish12(wire, &removed));
        CHECK_EQ(removed, driver_internal_error);
        backend.shut_down();
        shares->release(1u, shown_key);
    device11->retire_shares();
    }
}

TEST(d3d12_session_device, heap_windows_are_offered_over_memory_the_host_can_open_a_heap_on_and_shared_by_block)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    std::uint32_t supported = 0u;
    CHECK(av_d3d12_QueryHeapSlices12(wire, &supported));
    CHECK_EQ(supported, static_cast<std::uint32_t>(av::HeapSliceSupport::windows));

    av::MockPlatform        platform;
    av::EventfdWake         wake;
    av::D3d12QueueProgress  progress(wake, platform);
    av::DriverLog           lines(rig.sink, "windows: ");
    av::D3d12BlockHeaps     windows(platform, progress, lines);
    const std::uint64_t     memory = wire.open_memory(upload_memory, 2u * heap_bytes);
    av::SourceMemory *const store = rig.imports.named(memory);
    CHECK(store != nullptr);
    CHECK_EQ(windows.count(), 0u);
    ID3D12Heap *const first = windows.open(*device12, *store, 9u, 0u, heap_bytes);
    CHECK(first != nullptr);
    CHECK_EQ(windows.count(), 1u);
    ID3D12Heap *const second = windows.open(*device12, *store, 10u, heap_bytes, heap_bytes);
    CHECK(second == first);
    CHECK_EQ(windows.count(), 1u);
    std::uint64_t base = 1u, extent = 1u;
    CHECK(windows.window(10u, base, extent));
    CHECK_EQ(base, heap_bytes);
    CHECK_EQ(extent, heap_bytes);
    CHECK(!windows.window(11u, base, extent));
    CHECK(windows.open(*device12, *store, 9u, 0u, heap_bytes) == nullptr);
    CHECK(windows.open(*device12, *store, 12u, 100u, heap_bytes) == nullptr);
    CHECK(windows.open(*device12, *store, 12u, 0u, 0u) == nullptr);
    CHECK(windows.open(*device12, *store, 12u, heap_bytes, 4u * heap_bytes) == nullptr);
    CHECK(windows.attach(50u, 9u));
    CHECK(windows.attach(51u, 10u));
    CHECK(!windows.attach(52u, 99u));
    CHECK(windows.invariants_hold());
    windows.abandon(*store);
    windows.release_heap(9u);
    windows.release_heap(10u);
    windows.detach(50u);
    windows.reclaim();
    CHECK_EQ(windows.count(), 1u);
    windows.detach(51u);
    windows.detach(51u);
    windows.reclaim();
    CHECK_EQ(windows.count(), 0u);
    CHECK(!windows.window(9u, base, extent));
    CHECK(windows.invariants_hold());
    wire.close_memory(memory);
    backend.shut_down();
}

namespace {

constexpr std::uint32_t copied_to = vertices;
constexpr std::uint32_t copied_from = constants;
constexpr std::uint64_t copied_bytes = 4096u;

void make_copy(av::D3dLoopback &wire)
{
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateCommandQueue12(wire, queue, 0u, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, allocator, 0u));
    CHECK(av_d3d12_CreateCommandList12(wire, list, allocator, 0u, 0u));
    CHECK(av_d3d12_CreateCommittedResource12(wire, copied_to, heap_of(heap_default), 0u, buffer_of(copied_bytes), 0u,
                                              ClearValue{}, 0x300000000ull, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateCommittedResource12(wire, copied_from, heap_of(heap_default), 0u, buffer_of(copied_bytes), 0u,
                                              ClearValue{}, 0x400000000ull, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CopyBufferRegion12(wire, list, copied_to, 0u, copied_from, 0u, copied_bytes));
    CHECK(av_d3d12_CloseCommandList12(wire, list));
}

void judge_healthy(av::D3dLoopback &wire)
{
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    CHECK_EQ(wire.refused(), 0u);
}

}

TEST(d3d12_session_device, a_committed_resource_takes_every_heap_flag_combination_the_api_allows_it)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    constexpr std::uint32_t deny_buffers = 0x4u;
    constexpr std::uint32_t deny_rt_ds = 0x40u;
    constexpr std::uint32_t deny_non_rt_ds = 0x80u;
    ResourceDesc texture = {};
    texture.width = size;
    texture.dimension = dimension_texture2d;
    texture.height = size;
    texture.depth_or_array = 1u;
    texture.mip_levels = 1u;
    texture.format = format_b8g8r8a8;
    texture.sample_count = 1u;
    ResourceDesc target = texture;
    target.flags = flag_render_target;
    struct Case {
        const char *name;
        ResourceDesc desc;
        std::uint32_t flags;
    };
    const Case cases[] = {{"buffer only buffers", buffer_of(4096u), deny_rt_ds | deny_non_rt_ds},
                          {"buffer deny rt ds", buffer_of(4096u), deny_rt_ds},
                          {"buffer deny non rt ds", buffer_of(4096u), deny_non_rt_ds},
                          {"texture only non rt ds", texture, deny_buffers | deny_rt_ds},
                          {"texture deny buffers", texture, deny_buffers},
                          {"texture deny rt ds", texture, deny_rt_ds},
                          {"render target only rt ds", target, deny_buffers | deny_non_rt_ds}};
    std::uint32_t id = 900u;
    for (const Case &one : cases) {
        hresult = 1u;
        CHECK(av_d3d12_CreateCommittedResource12(wire, id, heap_of(heap_default), one.flags, one.desc, 0u,
                                                  ClearValue{}, one.desc.dimension == dimension_buffer ? 0x500000000ull + id * 0x10000ull : 0u, &hresult));
        if (hresult != 0u) std::fprintf(stderr, "committed %s: %08x\n", one.name, hresult);
        CHECK_EQ(hresult, 0u);
        id++;
        hresult = 1u;
        CHECK(av_d3d12_CreateHeap12(wire, id, heap_of(heap_default), one.flags, 1u << 20u, 65536u, 0u, 0u, &hresult));
        if (hresult != 0u) std::fprintf(stderr, "heap %s: %08x\n", one.name, hresult);
        CHECK_EQ(hresult, 0u);
        CHECK(av_d3d12_CreatePlacedResource12(wire, id + 1u, id, 0u, one.desc, 0u, ClearValue{},
                                               one.desc.dimension == dimension_buffer ? 0x600000000ull + id * 0x10000ull : 0u));
        id += 2u;
    }
    judge_healthy(wire);
    backend.shut_down();
}

TEST(d3d12_session_device, a_list_runs_its_copy_after_the_guest_destroyed_the_buffers_it_names)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    make_copy(wire);
    CHECK(av_d3d12_Destroy12(wire, copied_to));
    CHECK(av_d3d12_Destroy12(wire, copied_from));
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    judge_healthy(wire);
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    judge_healthy(wire);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

TEST(d3d12_session_device, buffers_and_a_list_destroyed_while_the_gpu_runs_them_are_released_after_it)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    make_copy(wire);
    const std::uint32_t lists[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, lists));
    CHECK(av_d3d12_Destroy12(wire, copied_to));
    CHECK(av_d3d12_Destroy12(wire, copied_from));
    CHECK(av_d3d12_Destroy12(wire, list));
    CHECK(av_d3d12_Destroy12(wire, allocator));
    CHECK(av_d3d12_Destroy12(wire, queue));
    judge_healthy(wire);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}

TEST(d3d12_session_device, a_heap_past_what_the_host_makes_is_answered_out_of_memory_and_the_device_lives)
{
    av::ExecutorLogRig rig;
    av::LoaderHostApis apis(rig.log);
    av::D3d11HostDevice *device11 = nullptr;
    av::D3d12HostDevice *device12 = nullptr;
    av::HostShares      *shares = nullptr;
    av::HostMemorySource *source = nullptr;
    if (!host_devices(apis, device11, device12, shares, source)) return;
    av::D3dBackend backend(rig.log);
    av::SessionShares session;
    CHECK(backend.initialize(*shared_as(session, shares, backend), rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, source, region_bytes, lent_bytes);
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, heap_of(heap_default), 0u, std::uint64_t{17} << 30u, 0u, 0u, 0u,
                                &hresult));
    CHECK_EQ(hresult, 0x8007000Eu);
    judge_healthy(wire);
    backend.shut_down();
}
