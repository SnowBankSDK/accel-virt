#include "miniport/adapter/capset_query.hpp"
#include "miniport/adapter/device_identity.hpp"
#include "miniport/av_context/context_memory.hpp"
#include "proto/resources/guest_page_list.hpp"
#include "miniport/av_context/context_pages.hpp"
#include "miniport/av_context/guest_context.hpp"
#include "core/support/status.hpp"
#include "device/module/driver_status.hpp"
#include "harness/executor/mock_executor_ops.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/host/mock_host.hpp"
#include "harness/host/mock_request.hpp"
#include "harness/loopback/loopback_rig.hpp"
#include "harness/context/guest_ring.hpp"
#include "proto/context/capset_info.hpp"
#include "proto/submission/fault_code.hpp"
#include "proto/context/context_setup.hpp"
#include "proto/context/protocol_version.hpp"
#include "proto/context/sub_api.hpp"
#include "proto/submission/command_header.hpp"
#include "proto/submission/reply_header.hpp"
#include "proto/virtio_gpu/gpu_response.hpp"
#include "proto/virtio_gpu/gpu_response_reader.hpp"
#include "proto/virtio_gpu/av_request_writer.hpp"

#include <cstdint>
#include <cstring>

using av::CapsetInfo;
using av::CapsetQuery;
using av::CommandHeader;
using av::ContextMemory;
using av::ContextSetup;
using av::GpuResponse;
using av::GpuResponseReader;
using av::GuestContext;
using av::GuestRing;
using av::LoopbackRig;
using av::ProtocolVersion;
using av::ReplyHeader;
using av::AvRequestWriter;
using av::Status;
using av::SubApi;

namespace {
constexpr std::size_t   capset_major_offset = 28u;
constexpr std::uint32_t large_batch = 1024u;

std::uint32_t answer_type(LoopbackRig &rig, const AvRequestWriter &writer, const std::uint8_t *request)
{
    std::uint8_t      answer[256] = {};
    const std::size_t answered = rig.device().exchange(request, writer.length(), answer, sizeof answer);
    GpuResponseReader reader;
    return reader.decode(answer, answered) == Status::ok ? reader.type() : 0u;
}

}

TEST(loopback_capset, the_driver_reads_the_devices_capset_and_refuses_another_major)
{ LoopbackRig rig;
  CHECK(rig.device().device().realized());
  CHECK(rig.read_capset());
  CapsetQuery other(rig.platform());
  rig.driver().tamper_answer(2u, capset_major_offset, static_cast<std::uint8_t>(ProtocolVersion::current_major + 1u));
  CHECK(rig.driver().query(other) != Status::ok);
  CHECK(other.failed());
  CHECK(other.invariants_hold()); }

TEST(loopback_capset, offers_host_visible_memory_and_no_retired_memory_path)
{ constexpr std::uint64_t gpu_exported_memory = 2u;
  constexpr std::uint64_t guest_memory_import = 4u;
  LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.capset().has_feature(CapsetInfo::feature_host_visible_memory));
  CHECK(!rig.capset().has_feature(gpu_exported_memory));
  CHECK(!rig.capset().has_feature(guest_memory_import));
  CHECK(rig.establish(0u));
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  ring.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, ring.tail()), Status::ok);
  CHECK_STATUS(rig.serve(0u), Status::ok);
  CHECK(rig.retire(0u)); }

TEST(loopback_capset, the_device_refuses_a_setup_of_another_major)
{ LoopbackRig rig;
  ContextMemory memory(rig.platform());
  CHECK_STATUS(memory.allocate(LoopbackRig::ring_order, LoopbackRig::reply_bytes), Status::ok);
  std::uint8_t     request[AvRequestWriter::max_request_size] = {};
  AvRequestWriter writer(request, sizeof request);
  CHECK_STATUS(writer.create_context(5u, CapsetInfo::capset_id, "test", 4u), Status::ok);
  CHECK_EQ(answer_type(rig, writer, request), GpuResponse::ok_nodata);
  const av::ContextPages   pages = memory.pages();
  const av::GuestPageList  lists[3] = {pages.ring(), pages.fence(), pages.reply()};
  for (std::uint32_t blob = 0u; blob < 3u; blob++) {
      const std::uint64_t length = std::uint64_t{lists[blob].count()} * av::GuestPageList::page_bytes;
      CHECK_EQ(lists[blob].runs(), 1u);
      CHECK_STATUS(writer.guest_blob(40u + blob, lists[blob].frame(0u), static_cast<std::uint32_t>(length)),
                   Status::ok);
      CHECK_EQ(answer_type(rig, writer, request), GpuResponse::ok_nodata);
  }
  CHECK_STATUS(writer.setup(5u, ContextSetup(ProtocolVersion(ProtocolVersion::current_major + 1u, 0u), SubApi::vulkan, 40u, 12u, 41u, 3u, 42u)),
               Status::ok);
  CHECK(answer_type(rig, writer, request) != GpuResponse::ok_nodata);
  CHECK_STATUS(writer.setup(5u, ContextSetup(ProtocolVersion(ProtocolVersion::current_major, ProtocolVersion::current_minor), SubApi::vulkan, 40u, 12u, 41u, 3u, 42u)),
               Status::ok);
  CHECK_EQ(answer_type(rig, writer, request), GpuResponse::ok_nodata);
  CHECK_STATUS(writer.destroy_context(5u), Status::ok);
  CHECK_EQ(answer_type(rig, writer, request), GpuResponse::ok_nodata);
  for (std::uint32_t blob = 0u; blob < 3u; blob++) {
      CHECK_STATUS(writer.unref(40u + blob), Status::ok);
      CHECK_EQ(answer_type(rig, writer, request), GpuResponse::ok_nodata);
  }
  rig.settle();
  CHECK_EQ(rig.device().device().contexts(), 0u);
  CHECK_EQ(rig.device().device().blobs(), 0u); }

TEST(loopback_context, sets_up_runs_batches_with_a_reply_and_tears_down)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  CHECK(rig.context(0u).invariants_hold());
  CHECK_EQ(rig.device().device().contexts(), 1u);
  CHECK_EQ(rig.device().device().blobs(), 3u);
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  ring.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, ring.tail()), Status::ok);
  const std::uint64_t first_end = ring.tail();
  std::uint8_t payload[CommandHeader::encoded_size + 8u] = {};
  CHECK_STATUS(CommandHeader(GuestRing::filler_command, sizeof payload, 1u).encode(payload, sizeof payload),
               Status::ok);
  std::memcpy(payload + CommandHeader::encoded_size, "loopback", 8u);
  ring.write_batch(2u, payload, sizeof payload);
  CHECK_STATUS(rig.submit(0u, 2u, first_end, ring.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 2u));
  CHECK_EQ(rig.context(0u).completed(), 2u);
  CHECK(!rig.context(0u).faulted());
  CHECK_EQ(rig.device().executor().executed(), 2u);
  ReplyHeader reply;
  CHECK_STATUS(reply.decode(rig.reply(0u), ReplyHeader::encoded_size), Status::ok);
  CHECK_EQ(reply.fence_id(), 2u);
  CHECK_EQ(reply.size(), 8u);
  CHECK(std::memcmp(rig.reply(0u) + ReplyHeader::encoded_size, "loopback", 8u) == 0);
  CHECK(rig.retire(0u));
  rig.settle();
  CHECK_EQ(rig.device().device().contexts(), 0u);
  CHECK_EQ(rig.device().device().blobs(), 0u);
  CHECK(rig.context(0u).invariants_hold()); }

TEST(loopback_context, a_nop_round_trips_with_an_empty_answer_the_executor_never_sees)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  std::memset(rig.reply(0u), 0xEE, ReplyHeader::encoded_size + 1u);
  std::uint8_t payload[CommandHeader::encoded_size] = {};
  CHECK_STATUS(CommandHeader(CommandHeader::nop_id, sizeof payload, CommandHeader::generate_reply)
                   .encode(payload, sizeof payload),
               Status::ok);
  ring.write_batch(1u, payload, sizeof payload);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, ring.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  CHECK_EQ(rig.context(0u).completed(), 1u);
  CHECK(!rig.context(0u).faulted());
  CHECK_EQ(rig.device().executor().executed(), 0u);
  ReplyHeader reply;
  CHECK_STATUS(reply.decode(rig.reply(0u), ReplyHeader::encoded_size), Status::ok);
  CHECK_EQ(reply.fence_id(), 1u);
  CHECK_EQ(reply.size(), 0u);
  CHECK(!reply.too_large());
  CHECK_EQ(rig.reply(0u)[ReplyHeader::encoded_size], 0xEEu);
  CHECK(rig.retire(0u));
  CHECK(rig.context(0u).invariants_hold()); }

TEST(loopback_backpressure, a_full_ring_refuses_until_the_device_moves_its_head)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  rig.device().executor().hold();
  std::uint64_t begin = 0u;
  for (std::uint64_t fence = 1u; fence <= 3u; fence++) {
      ring.write_batch(fence, large_batch);
      CHECK_STATUS(rig.submit(0u, fence, begin, ring.tail()), Status::ok);
      begin = ring.tail();
  }
  CHECK(rig.device().holding(1u));
  const std::uint32_t stores = rig.device().doorbells().stores();
  CHECK_STATUS(rig.submit(0u, 4u, begin, begin + 16u + large_batch), Status::no_space);
  CHECK_EQ(rig.context(0u).tail(), begin);
  CHECK_EQ(rig.device().doorbells().stores(), stores);
  rig.device().executor().release();
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 3u));
  ring.write_batch(4u, large_batch);
  CHECK_STATUS(rig.submit(0u, 4u, begin, ring.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 4u));
  CHECK(rig.context(0u).invariants_hold()); }

TEST(loopback_faults, a_faulted_context_reports_its_code_while_another_completes_and_sets_up_again)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  CHECK(rig.establish(1u));
  GuestRing broken(rig.context(0u).ring(), LoopbackRig::ring_order);
  broken.write_batch(1u, 64u);
  broken.data(0u) = 0u;
  CHECK_STATUS(rig.submit(0u, 1u, 0u, broken.tail()), Status::ok);
  GuestRing healthy(rig.context(1u).ring(), LoopbackRig::ring_order);
  healthy.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(1u, 1u, 0u, healthy.tail()), Status::ok);
  CHECK(!rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  CHECK(rig.context(0u).faulted());
  CHECK_EQ(rig.context(0u).fault_code(), 3u);
  CHECK(rig.driver().serve_until(rig.context(1u), rig.line(1u), 1u));
  CHECK(!rig.context(1u).faulted());
  CHECK(rig.retire(0u));
  CHECK_EQ(rig.device().device().contexts(), 1u);
  CHECK(rig.establish(0u));
  GuestRing again(rig.context(0u).ring(), LoopbackRig::ring_order);
  again.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, again.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  CHECK(!rig.context(0u).faulted());
  CHECK(rig.context(0u).invariants_hold());
  CHECK(rig.context(1u).invariants_hold()); }

TEST(loopback_faults, an_executor_that_loses_the_device_loses_one_context_and_no_other)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  CHECK(rig.establish(1u));
  std::uint8_t lost[CommandHeader::encoded_size] = {};
  CHECK_STATUS(CommandHeader(av::MockExecutorOps::lost_id, sizeof lost, 1u).encode(lost, sizeof lost), Status::ok);
  GuestRing losing(rig.context(0u).ring(), LoopbackRig::ring_order);
  losing.write_batch(1u, lost, sizeof lost);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, losing.tail()), Status::ok);
  GuestRing healthy(rig.context(1u).ring(), LoopbackRig::ring_order);
  healthy.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(1u, 1u, 0u, healthy.tail()), Status::ok);

  CHECK(!rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  CHECK(rig.context(0u).faulted());
  CHECK_EQ(rig.context(0u).fault_code(), static_cast<std::uint32_t>(av::FaultCode::device_lost));
  CHECK(rig.driver().serve_until(rig.context(1u), rig.line(1u), 1u));
  CHECK(!rig.context(1u).faulted());

  CHECK(rig.retire(0u));
  CHECK(rig.retire(1u));
  rig.settle();
  CHECK_EQ(rig.device().device().contexts(), 0u);
  CHECK_EQ(rig.device().device().blobs(), 0u); }

TEST(loopback_reset, after_a_device_reset_serving_finds_nothing_more_and_a_new_context_sets_up)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  ring.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, ring.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  rig.device().device().reset();
  rig.settle();
  CHECK_EQ(rig.device().device().contexts(), 0u);
  CHECK_EQ(rig.device().device().blobs(), 0u);
  CHECK_STATUS(rig.serve(0u), Status::ok);
  CHECK_EQ(rig.context(0u).completed(), 1u);
  rig.device().device().status(av::DriverStatus::driver_ok);
  rig.platform().set_irql(av::Irql::dispatch);
  rig.context(0u).abandon();
  rig.platform().set_irql(av::Irql::passive);
  CHECK_STATUS(rig.context(0u).release(), Status::ok);
  CHECK(rig.establish(0u));
  GuestRing fresh(rig.context(0u).ring(), LoopbackRig::ring_order);
  fresh.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, fresh.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  CHECK(rig.context(0u).invariants_hold()); }

TEST(loopback_teardown, the_pages_of_a_context_destroyed_while_the_host_runs_its_batch_are_given_back_only_once_the_host_writes_them_no_more)
{ constexpr std::uint32_t context_id = 1u;
  constexpr std::uint32_t ring_resource = 10u;
  constexpr std::uint32_t fence_resource = 11u;
  constexpr std::uint32_t reply_resource = 12u;
  constexpr std::size_t   page = 4096u;
  LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  rig.device().executor().hold();
  ring.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, ring.tail()), Status::ok);
  CHECK(rig.device().holding(1u));
  std::uint8_t    bytes[4][AvRequestWriter::max_request_size] = {};
  AvRequestWriter destroying(bytes[0], sizeof bytes[0]);
  CHECK_STATUS(destroying.destroy_context(context_id), Status::ok);
  av::MockRequest destroyed(bytes[0], destroying.length());
  CHECK(rig.device().device().control(&destroyed, 0x0201u));
  CHECK(destroyed.answered());
  CHECK_EQ(destroyed.response_type(), GpuResponse::ok_nodata);
  AvRequestWriter ring_back(bytes[1], sizeof bytes[1]);
  CHECK_STATUS(ring_back.unref(ring_resource), Status::ok);
  av::MockRequest ring_given(bytes[1], ring_back.length());
  CHECK(rig.device().device().control(&ring_given, 0x0102u));
  CHECK(!ring_given.answered());
  CHECK_EQ(ring.head(), 0u);
  rig.device().executor().release();
  av::MockHost &host = rig.device().host();
  for (int round = 0; round < 5000 && !ring_given.answered(); round++)
      if (host.wait_for_main_loop_work(1u, 1u)) static_cast<void>(host.run_main_loop());
  CHECK(ring_given.answered());
  CHECK_EQ(ring_given.response_type(), GpuResponse::ok_nodata);
  AvRequestWriter fence_back(bytes[2], sizeof bytes[2]);
  CHECK_STATUS(fence_back.unref(fence_resource), Status::ok);
  av::MockRequest fence_given(bytes[2], fence_back.length());
  CHECK(rig.device().device().control(&fence_given, 0x0102u));
  CHECK(fence_given.answered());
  CHECK_EQ(fence_given.response_type(), GpuResponse::ok_nodata);
  AvRequestWriter reply_back(bytes[3], sizeof bytes[3]);
  CHECK_STATUS(reply_back.unref(reply_resource), Status::ok);
  av::MockRequest reply_given(bytes[3], reply_back.length());
  CHECK(rig.device().device().control(&reply_given, 0x0102u));
  CHECK(reply_given.answered());
  CHECK_EQ(reply_given.response_type(), GpuResponse::ok_nodata);
  std::uint8_t ring_then[page];
  std::uint8_t fence_then[page];
  std::uint8_t reply_then[page];
  std::memcpy(ring_then, ring.bytes(), page);
  std::memcpy(fence_then, rig.context(0u).fence_page(), page);
  std::memcpy(reply_then, rig.reply(0u), page);
  rig.settle();
  CHECK_EQ(rig.device().device().reaping(), 0u);
  CHECK(std::memcmp(ring_then, ring.bytes(), page) == 0);
  CHECK(std::memcmp(fence_then, rig.context(0u).fence_page(), page) == 0);
  CHECK(std::memcmp(reply_then, rig.reply(0u), page) == 0);
  CHECK(rig.device().device().invariants_hold()); }

TEST(loopback_status, a_fence_raised_after_driver_ok_is_cleared_reaches_no_notifier)
{ LoopbackRig rig;
  CHECK(rig.read_capset());
  CHECK(rig.establish(0u));
  GuestRing ring(rig.context(0u).ring(), LoopbackRig::ring_order);
  ring.write_batch(1u, 64u);
  CHECK_STATUS(rig.submit(0u, 1u, 0u, ring.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 1u));
  const std::uint64_t first_end = ring.tail();
  const std::uint32_t raised = rig.device().host().raised(rig.line(0u));
  rig.device().executor().hold();
  ring.write_batch(2u, 64u);
  CHECK_STATUS(rig.submit(0u, 2u, first_end, ring.tail()), Status::ok);
  CHECK(rig.device().holding(1u));
  rig.device().device().status(0x0Bu);
  rig.device().executor().release();
  CHECK(!rig.device().raised_past(rig.line(0u), raised));
  CHECK_STATUS(rig.serve(0u), Status::ok);
  CHECK_EQ(rig.context(0u).completed(), 2u);
  CHECK(rig.device().device().invariants_hold());
  rig.device().device().status(av::DriverStatus::driver_ok);
  const std::uint64_t second_end = ring.tail();
  ring.write_batch(3u, 64u);
  CHECK_STATUS(rig.submit(0u, 3u, second_end, ring.tail()), Status::ok);
  CHECK(rig.driver().serve_until(rig.context(0u), rig.line(0u), 3u));
  CHECK(rig.context(0u).invariants_hold()); }

TEST(loopback_identity, a_device_bound_to_another_gpu_is_another_device)
{ constexpr std::uint8_t other_gpu[16] = {0x20, 0x0C};
  LoopbackRig rig;
  CHECK(rig.read_capset());
  av::DeviceIdentity identity(rig.platform());
  identity.remember(rig.capset());
  CHECK(identity.accept(rig.capset()));
  CHECK(!identity.changed());

  LoopbackRig replaced(other_gpu);
  CHECK(replaced.read_capset());
  CHECK(!identity.accept(replaced.capset()));
  CHECK(identity.changed());
  av::DeviceIdentity started_again(replaced.platform());
  started_again.remember(replaced.capset());
  CHECK(started_again.accept(replaced.capset()));
  CHECK(!started_again.changed()); }
