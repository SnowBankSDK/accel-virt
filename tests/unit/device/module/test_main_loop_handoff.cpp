#include "core/support/status.hpp"
#include "core/support/text_buffer.hpp"
#include "device/module/driver_status.hpp"
#include "device/module/virtio_gpu_av.hpp"
#include "harness/context/ring_blob_writer.hpp"
#include "harness/executor/guarded_executor_ops.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/host/gpu_request_builder.hpp"
#include "harness/host/main_loop_pump.hpp"
#include "harness/host/mock_host.hpp"
#include "harness/host/mock_request.hpp"
#include "proto/context/context_setup.hpp"
#include "proto/context/protocol_version.hpp"
#include "proto/context/sub_api.hpp"
#include "proto/resources/host_blob_id.hpp"
#include "proto/resources/host_memory_request.hpp"

#include <cstdint>
#include <cstring>

namespace av {

struct VirtioGpuAvProbe {
    static void write_ring_header(VirtioGpuAv &device, std::uint32_t id) { RingBlobWriter(device.blobs_).write_header(id, 12u); }
};

}

using av::GpuRequestBuilder;
using av::GuardedExecutorOps;
using av::MockHost;
using av::MockRequest;
using av::Status;
using av::VirtioGpuAv;

namespace {

constexpr std::uint8_t  gpu[16] = {0x31u};
constexpr std::uint32_t ok_nodata = 0x1100u;
constexpr std::uint32_t ok_map_info = 0x1106u;
constexpr std::uint64_t ring_ram = 0x200000u;
constexpr std::uint64_t lent_ram = 0x300000u;
constexpr std::uint32_t host_blob = av::HostBlobId::first;
constexpr std::uint32_t upload = static_cast<std::uint32_t>(av::HostMemoryRequest::Kind::upload);
constexpr std::uint32_t readback = static_cast<std::uint32_t>(av::HostMemoryRequest::Kind::readback);

GpuRequestBuilder guest_blob(std::uint32_t id, std::uint64_t address, std::uint32_t bytes)
{
    GpuRequestBuilder create(0x010Cu);
    create.u32(id).u32(1u).u32(0u).u32(1u).u64(0u).u64(bytes).u64(address).u32(bytes).u32(0u);
    return create;
}

GpuRequestBuilder host_blob_of(std::uint32_t context, std::uint32_t id, std::uint32_t memory, std::uint32_t request,
                               std::uint32_t flags, std::uint64_t bytes)
{
    GpuRequestBuilder create(0x010Cu);
    create.context(context).u32(id).u32(memory).u32(flags).u32(0u).u64(request).u64(bytes);
    return create;
}

GpuRequestBuilder named(std::uint32_t type, std::uint32_t id)
{
    GpuRequestBuilder request(type);
    request.u32(id).u32(0u);
    return request;
}

GpuRequestBuilder context_create(std::uint32_t id)
{
    GpuRequestBuilder create(0x0200u);
    create.context(id).u32(0u).u32(0x72u).zeros(64u);
    return create;
}

GpuRequestBuilder context_destroy(std::uint32_t id)
{
    GpuRequestBuilder destroy(0x0201u);
    destroy.context(id);
    return destroy;
}

GpuRequestBuilder bind(std::uint32_t id)
{
    std::uint8_t setup[av::ContextSetup::encoded_size] = {};
    static_cast<void>(av::ContextSetup(av::ProtocolVersion(av::ProtocolVersion::current_major, 0u), av::SubApi::vulkan,
                                       1u, 12u, 2u, 0u, 0u)
                          .encode(setup, sizeof setup));
    GpuRequestBuilder submit(0x0207u);
    submit.context(id).u32(static_cast<std::uint32_t>(sizeof setup)).u32(0u).append(setup, sizeof setup);
    return submit;
}

GpuRequestBuilder attach(std::uint32_t id, std::uint64_t address, std::uint32_t bytes)
{
    GpuRequestBuilder request(0x0106u);
    request.u32(id).u32(1u).u64(address).u32(bytes).u32(0u);
    return request;
}

GpuRequestBuilder shared_release(std::uint64_t object)
{
    GpuRequestBuilder release(0x02F1u);
    release.u64(object);
    return release;
}

class Rig {
public:
    Rig() : guard_(host_, AV_EXECUTOR_SUB_API_VULKAN), device_(host_)
    {
        host_.add_gpu(gpu);
        host_.set_hostmem_size(0x40000u);
        host_.add_guest_ram(ring_ram, 0x4000u);
        host_.add_guest_ram(lent_ram, 0x4000u);
        char       bytes[96];
        av::TextBuffer message(bytes, sizeof bytes);
        CHECK_STATUS(device_.realize("auto", false, &guard_.table(), nullptr, nullptr, nullptr, 0u, 0u, message),
                     Status::ok);
        guard_.arm();
        device_.status(av::DriverStatus::driver_ok);
    }
    ~Rig()
    {
        guard_.unblock();
        device_.reset();
        CHECK(settle());
        CHECK_EQ(guard_.on_main_loop(), 0u);
        guard_.disarm();
    }

    bool settle() { return av::settle_device(host_, device_); }

    std::uint32_t send(const GpuRequestBuilder &builder)
    {
        MockRequest request(builder.bytes(), builder.size());
        if (!post(request, builder)) return 0u;
        CHECK(av::pump_main_loop(host_, [&request] { return request.answered() || request.dropped(); }));
        return request.response_type();
    }

    bool post(MockRequest &request, const GpuRequestBuilder &builder)
    {
        std::uint32_t type = 0u;
        std::memcpy(&type, builder.bytes(), sizeof type);
        return device_.control(&request, type);
    }

    void make_bound_context(std::uint32_t id)
    {
        CHECK_EQ(send(guest_blob(1u, ring_ram, 0x2000u)), ok_nodata);
        CHECK_EQ(send(guest_blob(2u, ring_ram + 0x2000u, 0x2000u)), ok_nodata);
        av::VirtioGpuAvProbe::write_ring_header(device_, 1u);
        CHECK_EQ(send(context_create(id)), ok_nodata);
        CHECK_EQ(send(bind(id)), ok_nodata);
    }

    MockHost           &host() { return host_; }
    GuardedExecutorOps &guard() { return guard_; }
    VirtioGpuAv        &device() { return device_; }

private:
    MockHost           host_;
    GuardedExecutorOps guard_;
    VirtioGpuAv        device_;
};

}

TEST(main_loop_handoff, every_command_and_every_lifecycle_entry_reaches_the_executor_off_the_main_loop_only)
{ Rig rig;
  rig.make_bound_context(7u);
  CHECK_EQ(rig.send(context_create(8u)), ok_nodata);
  GpuRequestBuilder info(0x0108u);
  info.u32(0u).u32(0u);
  CHECK(rig.send(info) != 0u);
  CHECK_EQ(rig.send(host_blob_of(7u, host_blob, 2u, upload, 1u, 0x2000u)), ok_nodata);
  GpuRequestBuilder map(0x0208u);
  map.u32(host_blob).u32(0u).u64(0x4000u);
  CHECK_EQ(rig.send(map), ok_map_info);
  CHECK_EQ(rig.send(named(0x0209u, host_blob)), ok_nodata);
  CHECK_EQ(rig.send(named(0x0102u, host_blob)), ok_nodata);
  CHECK_EQ(rig.send(host_blob_of(8u, host_blob + 1u, 3u, readback, 0u, 0x1000u)), ok_nodata);
  CHECK_EQ(rig.send(attach(host_blob + 1u, lent_ram, 0x1000u)), ok_nodata);
  CHECK_EQ(rig.send(named(0x0102u, host_blob + 1u)), ok_nodata);
  CHECK_EQ(rig.send(shared_release(0x51u)), ok_nodata);
  CHECK_EQ(rig.send(context_destroy(8u)), ok_nodata);
  CHECK_EQ(rig.send(host_blob_of(7u, host_blob + 2u, 2u, upload, 1u, 0x1000u)), ok_nodata);
  rig.device().reset();
  CHECK(rig.settle());
  rig.device().status(av::DriverStatus::driver_ok);
  rig.make_bound_context(9u);
  CHECK_EQ(rig.send(context_destroy(9u)), ok_nodata);
  CHECK(rig.settle());
  CHECK_EQ(rig.guard().on_main_loop(), 0u);
  CHECK_EQ(rig.guard().first_on_main_loop(), 0u);
  CHECK(rig.guard().off_main_loop() != 0u);
  CHECK_EQ(rig.guard().inner().memory_allocations(), 3u);
  CHECK_EQ(rig.guard().inner().shared_releases(), 2u);
  CHECK_EQ(rig.host().unclaimed_violations(), 0u); }

TEST(main_loop_handoff, a_second_context_is_answered_while_the_first_ones_blob_create_is_inside_the_driver)
{ Rig rig;
  CHECK_EQ(rig.send(context_create(7u)), ok_nodata);
  CHECK_EQ(rig.send(context_create(8u)), ok_nodata);
  rig.guard().block();
  const GpuRequestBuilder create = host_blob_of(7u, host_blob, 2u, upload, 1u, 0x2000u);
  MockRequest             blocked(create.bytes(), create.size());
  CHECK(rig.post(blocked, create));
  CHECK(rig.guard().await_blocked(1u, 5000u));
  CHECK(!blocked.answered());
  CHECK_EQ(rig.send(host_blob_of(8u, host_blob + 1u, 3u, readback, 0u, 0x1000u)), ok_nodata);
  CHECK_EQ(rig.send(context_create(9u)), ok_nodata);
  CHECK_EQ(rig.send(context_destroy(9u)), ok_nodata);
  CHECK_EQ(rig.send(named(0x0102u, host_blob + 1u)), ok_nodata);
  GpuRequestBuilder info(0x0108u);
  info.u32(0u).u32(0u);
  CHECK(rig.send(info) != 0u);
  CHECK(!blocked.answered());
  CHECK_EQ(rig.device().outstanding(), 3u);
  rig.guard().unblock();
  CHECK(rig.settle());
  CHECK(blocked.answered());
  CHECK_EQ(blocked.response_type(), ok_nodata);
  CHECK_EQ(rig.guard().inner().held_memory(), 1u);
  CHECK_EQ(rig.guard().on_main_loop(), 0u); }

TEST(main_loop_handoff, a_reset_while_a_blob_create_is_inside_the_driver_drops_its_answer_and_frees_what_it_made)
{ Rig rig;
  CHECK_EQ(rig.send(context_create(7u)), ok_nodata);
  rig.guard().block();
  const GpuRequestBuilder create = host_blob_of(7u, host_blob, 2u, upload, 1u, 0x2000u);
  MockRequest             blocked(create.bytes(), create.size());
  CHECK(rig.post(blocked, create));
  CHECK(rig.guard().await_blocked(1u, 5000u));
  rig.device().reset();
  CHECK(blocked.dropped());
  CHECK(!blocked.answered());
  CHECK_EQ(rig.host().kept_requests(), 0u);
  rig.device().status(av::DriverStatus::driver_ok);
  CHECK_EQ(rig.send(context_create(7u)), ok_nodata);
  rig.guard().unblock();
  CHECK(rig.settle());
  CHECK(!blocked.answered());
  CHECK_EQ(rig.guard().inner().held_memory(), 0u);
  CHECK_EQ(rig.guard().inner().live_memory(), 0u);
  CHECK_EQ(rig.host().dropped_requests(), 1u);
  CHECK(rig.device().invariants_hold()); }
