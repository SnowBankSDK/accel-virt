#include "executor/d3d/d3d_backend.hpp"
#include "executor/owned_fd.hpp"
#include "executor/host_memory_source.hpp"
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

using namespace av::d3d12proto;

namespace {

constexpr std::uint32_t heap_upload = 2u;
constexpr std::uint32_t heap_readback = 3u;
constexpr std::uint32_t state_copy_dest = 0x400u;
constexpr std::uint32_t state_generic_read = 0xac3u;
constexpr std::uint32_t dimension_buffer = 1u;
constexpr std::uint32_t layout_row_major = 1u;
constexpr std::size_t   region_bytes = 16u << 20u;
constexpr std::size_t   lent_bytes = 8u << 20u;
constexpr std::uint64_t heap_bytes = 64u * 1024u;
constexpr std::uint64_t buffer_bytes = 256u;

enum Id : std::uint32_t { queue = 1, allocator, list, renamed, upload_heap, readback_heap, source, destination };

const av::HostMemoryRequest upload_memory(av::HostMemoryRequest::Kind::upload, 0u);
const av::HostMemoryRequest readback_memory(av::HostMemoryRequest::Kind::readback, 0u);

bool host_devices(av::LoaderHostApis &apis, av::D3d11HostDevice *&device11, av::D3d12HostDevice *&device12,
                  av::HostShares *&shares, av::HostMemorySource *&memory)
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
        memory = apis.memory_source(index);
        if (device11 != nullptr && device12 != nullptr && memory != nullptr) return true;
    }
    std::fprintf(stderr, "no GPU here runs vkd3d-proton: nothing judged\n");
    return false;
}

bool imports(av::D3d12HostDevice &device, av::D3dLoopback &wire)
{
    const std::uint64_t           memory = wire.open_memory(readback_memory, heap_bytes);
    const av::SourceMemory *const held = wire.source_memory(memory);
    av::OwnedFd                   shared;
    const bool                    exported = held != nullptr && held->share(shared);
    ID3D12Heap *const imported =
        exported ? device.import_dmabuf_heap(held->pointer(), shared.fd(), held->size()) : nullptr;
    wire.close_memory(memory);
    if (imported == nullptr) {
        std::fprintf(stderr, "this vkd3d-proton opens no heap over a dma-buf: nothing judged\n");
        return false;
    }
    imported->Release();
    return true;
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

}

TEST(d3d12_lane_order_device, a_list_recorded_again_while_its_execute_waits_on_a_lane_runs_what_it_recorded_and_reads_what_the_cpu_wrote_first)
{
    av::ExecutorLogRig    rig;
    av::LoaderHostApis    apis(rig.log);
    av::D3d11HostDevice  *device11 = nullptr;
    av::D3d12HostDevice  *device12 = nullptr;
    av::HostShares       *shares = nullptr;
    av::HostMemorySource *memory = nullptr;
    if (!host_devices(apis, device11, device12, shares, memory)) return;
    av::D3dBackend    backend(rig.log);
    av::SessionShares session;
    session.bind(*shares, 1u, backend);
    CHECK(backend.initialize(session, rig.imports, device11, device12));
    av::D3dLoopback wire(backend, rig.imports, memory, region_bytes, lent_bytes);
    if (!imports(*device12, wire)) {
        backend.shut_down();
        return;
    }
    std::uint32_t hresult = 1u;
    CHECK(av_d3d12_OpenDevice12(wire, 0u, &hresult));
    CHECK_EQ(hresult, 0u);
    CHECK(av_d3d12_CreateCommandQueue12(wire, queue, 0u, 0u, 0u));
    CHECK(av_d3d12_CreateCommandAllocator12(wire, allocator, 0u));
    const std::uint64_t uploads = wire.open_memory(upload_memory, heap_bytes);
    const std::uint64_t readbacks = wire.open_memory(readback_memory, heap_bytes);
    CHECK(uploads != 0u && readbacks != 0u);
    std::memset(wire.memory(uploads), 0x11, buffer_bytes);
    std::memset(wire.memory(readbacks), 0xcd, buffer_bytes);
    CHECK(av_d3d12_CreateHeap12(wire, upload_heap, HeapProperties{heap_upload, 0u, 0u, 0u}, 0u, heap_bytes, 0u, uploads, 0u));
    CHECK(av_d3d12_CreateHeap12(wire, readback_heap, HeapProperties{heap_readback, 0u, 0u, 0u}, 0u, heap_bytes, 0u,
                                readbacks, 0u));
    CHECK(av_d3d12_CreatePlacedResource12(wire, source, upload_heap, 0u, buffer_of(buffer_bytes), state_generic_read,
                                           ClearValue{}, 0x200000000ull));
    CHECK(av_d3d12_CreatePlacedResource12(wire, destination, readback_heap, 0u, buffer_of(buffer_bytes), state_copy_dest,
                                           ClearValue{}, 0x100000000ull));
    CHECK(av_d3d12_CreateCommandList12(wire, list, allocator, 0u, 0u));
    CHECK(av_d3d12_CopyBufferRegion12(wire, list, destination, 0u, source, 0u, 16u));
    CHECK(av_d3d12_CloseCommandList12(wire, list));

    CHECK(av_d3d12_CreateCommandList12(wire, renamed, allocator, 0u, 0u));
    CHECK(av_d3d12_CopyBufferRegion12(wire, renamed, destination, 64u, source, 64u, 16u));
    CHECK(av_d3d12_CloseCommandList12(wire, renamed));
    std::memset(wire.memory(uploads), 0x5a, 16u);

    const std::uint32_t first[1] = {list};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, first));
    CHECK(av_d3d12_Destroy12(wire, list));
    std::uint32_t removed = 1u;
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    for (std::uint32_t at = 0u; at < 16u; at++) CHECK_EQ(wire.memory(readbacks)[at], 0x5au);
    for (std::uint32_t at = 64u; at < 80u; at++) CHECK_EQ(wire.memory(readbacks)[at], 0xcdu);

    const std::uint32_t second[1] = {renamed};
    CHECK(av_d3d12_ExecuteCommandLists12(wire, queue, 1u, second));
    CHECK(av_d3d12_Finish12(wire, &removed));
    CHECK_EQ(removed, 0u);
    for (std::uint32_t at = 64u; at < 80u; at++) CHECK_EQ(wire.memory(readbacks)[at], 0x11u);
    for (std::uint32_t at = 0u; at < 16u; at++) CHECK_EQ(wire.memory(readbacks)[at], 0x5au);
    CHECK_EQ(wire.refused(), 0u);
    CHECK(backend.invariants_hold());
    backend.shut_down();
}
