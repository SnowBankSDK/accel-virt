#include "core/support/status.hpp"
#include "device/abi/av_executor.h"
#include "device/isolation/renderer_link.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/host/mock_host.hpp"
#include "harness/host/recording_late_answers.hpp"
#include "harness/host/renderer_peer.hpp"
#include "harness/host/scripted_renderer_peer.hpp"
#include "proto/renderer/context_message.hpp"
#include "proto/renderer/ready_message.hpp"
#include "proto/renderer/renderer_message_header.hpp"
#include "proto/renderer/reply_message.hpp"

#include <cstdint>
#include <cstring>

using av::MockHost;
using av::RendererLink;
using av::RendererMessageHeader;
using av::ScriptedRendererPeer;
using av::Status;
using Kind = av::RendererMessageHeader::Kind;

namespace {

constexpr std::uint64_t millisecond = 1000000u;

class Fixture {
public:
    Fixture() : peer_(host_), answers_(), link_(host_, answers_)
    {
        host_.connect_renderer(peer_);
        static_cast<void>(host_.start_renderer("av-renderer"));
    }

    MockHost             &host() { return host_; }
    ScriptedRendererPeer &peer() { return peer_; }
    av::RecordingLateAnswers &answers() { return answers_; }
    RendererLink         &link() { return link_; }

    void queue_late(Kind kind, std::uint64_t sequence, std::uint8_t first, std::uint32_t descriptors)
    {
        std::uint8_t datagram[RendererMessageHeader::encoded_size + 4u] = {};
        static_cast<void>(RendererMessageHeader(kind, 4u, sequence).encode(datagram, sizeof datagram));
        datagram[RendererMessageHeader::encoded_size] = first;
        peer_.queue(datagram, sizeof datagram, descriptors);
    }
    Status create(Status &outcome)
    {
        std::uint8_t body[av::ContextMessage::encoded_size] = {};
        static_cast<void>(av::ContextMessage(5u).encode(body, sizeof body));
        return link_.request(Kind::create_context, body, sizeof body, nullptr, 0u, outcome);
    }

private:
    MockHost             host_;
    ScriptedRendererPeer peer_;
    av::RecordingLateAnswers answers_;
    RendererLink         link_;
};

class EndlessPeer final : public av::RendererPeer {
public:
    EndlessPeer() : answered_(0u) {}
    void deliver(const std::uint8_t *, std::size_t, const int *, std::uint32_t) override {}
    bool answer(std::uint8_t *buffer, std::size_t capacity, std::size_t &length, int *, std::uint32_t,
                std::uint32_t &count) override
    {
        count = 0u;
        answered_++;
        length = RendererMessageHeader::encoded_size;
        return RendererMessageHeader(Kind::exported_image, 0u, answered_).encode(buffer, capacity) == Status::ok;
    }
    std::uint32_t answered() const { return answered_; }

private:
    std::uint32_t answered_;
};

}

TEST(renderer_link_post, frames_each_message_with_the_next_sequence_and_its_body)
{ Fixture fixture;
  const std::uint8_t body[3] = {0xA1u, 0xB2u, 0xC3u};
  const int          sent = fixture.host().open_descriptor();
  CHECK_STATUS(fixture.link().post(Kind::region, body, sizeof body, &sent, 1u), Status::ok);
  CHECK_STATUS(fixture.link().post(Kind::interrupts_off, nullptr, 0u, nullptr, 0u), Status::ok);
  CHECK_EQ(fixture.peer().delivered(), 2u);
  CHECK(fixture.peer().kind_at(0u) == Kind::region);
  CHECK_EQ(fixture.peer().sequence_at(0u), 1u);
  CHECK_EQ(fixture.peer().body_length_at(0u), 3u);
  CHECK(std::memcmp(fixture.peer().body_at(0u), body, sizeof body) == 0);
  CHECK_EQ(fixture.peer().descriptors_at(0u), 1u);
  CHECK_EQ(fixture.peer().open_descriptors_at(0u), 1u);
  CHECK(fixture.peer().kind_at(1u) == Kind::interrupts_off);
  CHECK_EQ(fixture.peer().sequence_at(1u), 2u);
  CHECK_EQ(fixture.peer().body_length_at(1u), 0u);
  CHECK(fixture.host().descriptor_open(sent));
  fixture.host().close_descriptor(sent);
  CHECK_EQ(fixture.peer().queued(), 0u); }

TEST(renderer_link_post, refuses_a_body_too_long_or_missing)
{ Fixture fixture;
  static std::uint8_t body[RendererMessageHeader::max_body_length + 1u];
  CHECK_STATUS(fixture.link().post(Kind::guest_blob, body, RendererMessageHeader::max_body_length + 1u, nullptr, 0u),
               Status::invalid_parameter);
  CHECK_STATUS(fixture.link().post(Kind::region, nullptr, 1u, nullptr, 0u), Status::invalid_parameter);
  CHECK_EQ(fixture.peer().delivered(), 0u);
  CHECK_STATUS(fixture.link().post(Kind::guest_blob, body, RendererMessageHeader::max_body_length, nullptr, 0u),
               Status::ok);
  CHECK_EQ(fixture.peer().delivered(), 1u);
  CHECK_EQ(fixture.peer().sequence_at(0u), 1u); }

TEST(renderer_link_post, a_kind_the_protocol_does_not_have_is_not_framed_or_sent)
{ Fixture fixture;
  const std::uint8_t body[4] = {1u, 2u, 3u, 4u};
  CHECK_STATUS(fixture.link().post(static_cast<Kind>(0x7777u), body, sizeof body, nullptr, 0u),
               Status::invalid_parameter);
  CHECK_EQ(fixture.peer().delivered(), 0u); }

TEST(renderer_link_post, a_renderer_that_is_gone_is_not_found)
{ Fixture fixture;
  fixture.host().renderer_gone();
  CHECK_STATUS(fixture.link().post(Kind::interrupts_off, nullptr, 0u, nullptr, 0u), Status::not_found);
  CHECK_EQ(fixture.peer().delivered(), 0u); }

TEST(renderer_link_request, answers_with_the_outcome_the_renderer_replied)
{ Fixture fixture;
  Status outcome = Status::invalid_state;
  CHECK_STATUS(fixture.create(outcome), Status::ok);
  CHECK_STATUS(outcome, Status::ok);
  fixture.peer().answer_requests_with(Status::conflict);
  CHECK_STATUS(fixture.create(outcome), Status::ok);
  CHECK_STATUS(outcome, Status::conflict);
  CHECK_EQ(fixture.peer().delivered(), 2u);
  CHECK_EQ(fixture.peer().sequence_at(1u), 2u);
  CHECK_EQ(fixture.host().last_receive_timeout(), RendererLink::request_timeout_ms); }

TEST(renderer_link_request, a_renderer_gone_before_the_send_or_during_the_wait_is_not_found)
{ Fixture fixture;
  Status outcome = Status::ok;
  fixture.host().renderer_gone();
  CHECK_STATUS(fixture.create(outcome), Status::not_found);
  CHECK_STATUS(outcome, Status::invalid_state);
  CHECK_EQ(fixture.peer().delivered(), 0u);
  CHECK_STATUS(fixture.host().start_renderer("av-renderer"), Status::ok);
  fixture.peer().die_on_next_delivery();
  outcome = Status::ok;
  CHECK_STATUS(fixture.create(outcome), Status::not_found);
  CHECK_STATUS(outcome, Status::invalid_state);
  CHECK_EQ(fixture.peer().delivered(), 1u); }

TEST(renderer_link_request, a_silent_renderer_is_invalid_state)
{ Fixture fixture;
  fixture.peer().fall_silent();
  Status outcome = Status::ok;
  CHECK_STATUS(fixture.create(outcome), Status::invalid_state);
  CHECK_STATUS(outcome, Status::invalid_state); }

TEST(renderer_link_request, skips_stale_replies_other_kinds_and_garbage_closing_their_descriptors)
{ Fixture fixture;
  fixture.peer().queue_reply(7u, Status::no_memory, 2u);
  std::uint8_t ready[RendererMessageHeader::encoded_size + av::ReadyMessage::encoded_size] = {};
  CHECK_STATUS(RendererMessageHeader(Kind::ready, av::ReadyMessage::encoded_size, 1u).encode(ready, sizeof ready),
               Status::ok);
  CHECK_STATUS(av::ReadyMessage(1u, 1u, 1u).encode(ready + RendererMessageHeader::encoded_size,
                                                             av::ReadyMessage::encoded_size),
               Status::ok);
  fixture.peer().queue(ready, sizeof ready, 1u);
  const std::uint8_t garbage[5] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
  fixture.peer().queue(garbage, sizeof garbage, 3u);
  fixture.peer().answer_requests_with(Status::not_supported);
  Status outcome = Status::ok;
  CHECK_STATUS(fixture.create(outcome), Status::ok);
  CHECK_STATUS(outcome, Status::not_supported);
  CHECK_EQ(fixture.peer().queued(), 0u);
  CHECK_EQ(fixture.host().live_descriptors(), 0u);
  CHECK_EQ(fixture.host().unclaimed_violations(), 0u); }

TEST(renderer_link_request, closes_descriptors_that_came_with_the_awaited_reply)
{ Fixture fixture;
  fixture.peer().fall_silent();
  fixture.peer().queue_reply(1u, Status::ok, 2u);
  Status outcome = Status::invalid_state;
  CHECK_STATUS(fixture.create(outcome), Status::ok);
  CHECK_STATUS(outcome, Status::ok);
  CHECK_EQ(fixture.host().live_descriptors(), 0u); }

TEST(renderer_link_request, a_reply_that_does_not_decode_or_names_another_request_is_malformed)
{ { Fixture fixture;
    fixture.peer().fall_silent();
    std::uint8_t bad[RendererMessageHeader::encoded_size + av::ReplyMessage::fixed_size] = {};
    CHECK_STATUS(RendererMessageHeader(Kind::reply, av::ReplyMessage::fixed_size, 1u).encode(bad, sizeof bad),
                 Status::ok);
    std::memset(bad + RendererMessageHeader::encoded_size, 0xEE, av::ReplyMessage::fixed_size);
    fixture.peer().queue(bad, sizeof bad, 0u);
    Status outcome = Status::ok;
    CHECK_STATUS(fixture.create(outcome), Status::malformed);
    CHECK_STATUS(outcome, Status::invalid_state); }
  Fixture fixture;
  fixture.peer().fall_silent();
  fixture.peer().queue_reply(1u, 2u, Status::ok);
  Status outcome = Status::ok;
  CHECK_STATUS(fixture.create(outcome), Status::malformed);
  CHECK_STATUS(outcome, Status::invalid_state); }

TEST(renderer_link_request, the_wait_ends_at_its_deadline_however_much_the_renderer_says)
{ Fixture fixture;
  fixture.peer().fall_silent();
  for (std::uint64_t stale = 10u; stale < 13u; stale++) fixture.peer().queue_reply(stale, Status::ok, 0u);
  fixture.host().advance_clock_per_read(1000u * millisecond);
  Status outcome = Status::ok;
  CHECK_STATUS(fixture.create(outcome), Status::invalid_state);
  CHECK_EQ(fixture.peer().queued(), 2u);
  CHECK_EQ(fixture.host().last_receive_timeout(), 1000u); }

TEST(renderer_link_request, a_wait_is_given_what_is_left_rounded_up_to_the_millisecond)
{ Fixture fixture;
  fixture.host().advance_clock_per_read(millisecond + millisecond / 2u);
  Status outcome = Status::invalid_state;
  CHECK_STATUS(fixture.create(outcome), Status::ok);
  CHECK_STATUS(outcome, Status::ok);
  CHECK_EQ(fixture.host().last_receive_timeout(), RendererLink::request_timeout_ms - 1u); }

TEST(renderer_link_request, refuses_a_body_it_cannot_frame_without_sending)
{ Fixture fixture;
  Status outcome = Status::ok;
  CHECK_STATUS(fixture.link().request(Kind::create_context, nullptr, 4u, nullptr, 0u, outcome),
               Status::invalid_parameter);
  CHECK_STATUS(outcome, Status::invalid_state);
  CHECK_EQ(fixture.peer().delivered(), 0u); }

TEST(renderer_link_greet, sends_the_hello_and_decodes_the_ready)
{ Fixture fixture;
  fixture.peer().ready_with(av::ReadyMessage(1u, 6u, 5u));
  const std::uint8_t body[4] = {1u, 2u, 3u, 4u};
  av::ReadyMessage   ready;
  CHECK_STATUS(fixture.link().greet(body, sizeof body, ready), Status::ok);
  CHECK(fixture.peer().kind_at(0u) == Kind::hello);
  CHECK_EQ(fixture.peer().descriptors_at(0u), 0u);
  CHECK_EQ(fixture.peer().body_length_at(0u), 4u);
  CHECK_EQ(fixture.host().last_receive_timeout(), RendererLink::hello_timeout_ms);
  CHECK_EQ(ready.version(), 1u);
  CHECK_EQ(ready.executor_abi(), 6u);
  CHECK_EQ(ready.sub_apis(), 5u); }

TEST(renderer_link_greet, a_silent_gone_or_garbled_renderer_is_refused)
{ { Fixture fixture;
    fixture.peer().fall_silent();
    av::ReadyMessage ready;
    const std::uint8_t body[1] = {1u};
    CHECK_STATUS(fixture.link().greet(body, sizeof body, ready), Status::invalid_state); }
  { Fixture fixture;
    fixture.host().renderer_gone();
    av::ReadyMessage ready;
    const std::uint8_t body[1] = {1u};
    CHECK_STATUS(fixture.link().greet(body, sizeof body, ready), Status::not_found); }
  { Fixture fixture;
    av::ReadyMessage ready;
    CHECK_STATUS(fixture.link().greet(nullptr, 1u, ready), Status::invalid_parameter);
    CHECK_EQ(fixture.peer().delivered(), 0u); }
  Fixture fixture;
  fixture.peer().fall_silent();
  std::uint8_t short_ready[RendererMessageHeader::encoded_size + av::ReadyMessage::encoded_size - 1u] = {};
  CHECK_STATUS(RendererMessageHeader(Kind::ready, av::ReadyMessage::encoded_size - 1u, 1u)
                   .encode(short_ready, sizeof short_ready),
               Status::ok);
  fixture.peer().queue(short_ready, sizeof short_ready, 0u);
  av::ReadyMessage ready;
  const std::uint8_t body[1] = {1u};
  CHECK_STATUS(fixture.link().greet(body, sizeof body, ready), Status::malformed);
  CHECK_EQ(ready.version(), 0u); }

TEST(renderer_link_exchange, keeps_the_answers_descriptors_up_to_capacity_and_closes_the_rest)
{ Fixture fixture;
  std::uint8_t       stale[RendererMessageHeader::encoded_size + 4u] = {};
  CHECK_STATUS(RendererMessageHeader(Kind::exported_image, 4u, 9u).encode(stale, sizeof stale), Status::ok);
  fixture.peer().queue(stale, sizeof stale, 2u);
  std::uint8_t answer_bytes[RendererMessageHeader::encoded_size + 4u] = {};
  CHECK_STATUS(RendererMessageHeader(Kind::exported_image, 4u, 1u).encode(answer_bytes, sizeof answer_bytes),
               Status::ok);
  answer_bytes[RendererMessageHeader::encoded_size] = 0x6Bu;
  fixture.peer().queue(answer_bytes, sizeof answer_bytes, 3u);
  const std::uint8_t body[8] = {1u};
  const std::uint8_t *answer = nullptr;
  std::uint32_t       length = 0u;
  int                 kept[2] = {-1, -1};
  std::uint32_t       count = 9u;
  CHECK_STATUS(fixture.link().exchange(Kind::export_image, body, sizeof body, Kind::exported_image, answer, length,
                                       kept, 2u, count),
               Status::ok);
  CHECK(fixture.peer().kind_at(0u) == Kind::export_image);
  CHECK_EQ(fixture.peer().descriptors_at(0u), 0u);
  CHECK_EQ(length, 4u);
  CHECK_EQ(answer[0], 0x6Bu);
  CHECK_EQ(count, 2u);
  CHECK(fixture.host().descriptor_open(kept[0]));
  CHECK(fixture.host().descriptor_open(kept[1]));
  CHECK_EQ(fixture.host().live_descriptors(), 2u);
  fixture.host().close_descriptor(kept[0]);
  fixture.host().close_descriptor(kept[1]);
  CHECK_EQ(fixture.host().unclaimed_violations(), 0u); }

TEST(renderer_link_exchange, fails_as_a_request_does)
{ { Fixture fixture;
    fixture.host().renderer_gone();
    const std::uint8_t *answer = nullptr;
    std::uint32_t       length = 0u, count = 5u;
    CHECK_STATUS(fixture.link().exchange(Kind::export_image, nullptr, 0u, Kind::exported_image, answer, length, nullptr,
                                         0u, count),
                 Status::not_found);
    CHECK_EQ(count, 0u); }
  { Fixture fixture;
    const std::uint8_t *answer = nullptr;
    std::uint32_t       length = 0u, count = 0u;
    CHECK_STATUS(fixture.link().exchange(Kind::export_image, nullptr, 3u, Kind::exported_image, answer, length, nullptr,
                                         0u, count),
                 Status::invalid_parameter);
    CHECK_EQ(fixture.peer().delivered(), 0u); }
  Fixture fixture;
  const std::uint8_t *answer = nullptr;
  std::uint32_t       length = 0u, count = 0u;
  CHECK_STATUS(fixture.link().exchange(Kind::export_image, nullptr, 0u, Kind::exported_image, answer, length, nullptr, 0u,
                                       count),
               Status::invalid_state); }

TEST(renderer_link_ask, frames_the_next_sequence_and_returns_without_waiting_for_the_answer)
{ Fixture fixture;
  const std::uint8_t body[2] = {0x11u, 0x22u};
  std::uint64_t      sequence = 0u;
  CHECK_STATUS(fixture.link().post(Kind::interrupts_off, nullptr, 0u, nullptr, 0u), Status::ok);
  CHECK_STATUS(fixture.link().ask(Kind::export_image, body, sizeof body, sequence), Status::ok);
  CHECK_EQ(sequence, 2u);
  CHECK_EQ(fixture.peer().delivered(), 2u);
  CHECK(fixture.peer().kind_at(1u) == Kind::export_image);
  CHECK_EQ(fixture.peer().sequence_at(1u), 2u);
  CHECK_EQ(fixture.peer().descriptors_at(1u), 0u);
  CHECK(std::memcmp(fixture.peer().body_at(1u), body, sizeof body) == 0);
  CHECK_EQ(fixture.host().last_receive_timeout(), 0u);
  CHECK_STATUS(fixture.link().ask(Kind::export_image, nullptr, 2u, sequence), Status::invalid_parameter);
  CHECK_EQ(fixture.peer().delivered(), 2u);
  fixture.host().renderer_gone();
  CHECK_STATUS(fixture.link().ask(Kind::export_image, body, sizeof body, sequence), Status::not_found); }

TEST(renderer_link_request, an_answer_no_wait_is_for_goes_to_the_late_taker_and_the_wait_goes_on)
{ Fixture fixture;
  fixture.answers().claim(Kind::exported_image, Status::ok);
  fixture.queue_late(Kind::exported_image, 41u, 0x5Au, 1u);
  fixture.queue_late(Kind::allocated_memory, 42u, 0x6Bu, 2u);
  Status outcome = Status::invalid_state;
  CHECK_STATUS(fixture.create(outcome), Status::ok);
  CHECK_STATUS(outcome, Status::ok);
  CHECK_EQ(fixture.answers().taken(), 1u);
  CHECK_EQ(fixture.answers().sequence_at(0u), 41u);
  CHECK_EQ(fixture.answers().descriptors_at(0u), 1u);
  CHECK_EQ(fixture.answers().first_byte_at(0u), 0x5Au);
  CHECK_EQ(fixture.peer().queued(), 0u);
  CHECK_EQ(fixture.host().live_descriptors(), 0u); }

TEST(renderer_link_request, a_late_answer_the_taker_finds_bad_ends_the_wait_with_its_status)
{ Fixture fixture;
  fixture.answers().claim(Kind::exported_image, Status::malformed);
  fixture.queue_late(Kind::exported_image, 41u, 0u, 1u);
  Status outcome = Status::ok;
  CHECK_STATUS(fixture.create(outcome), Status::malformed);
  CHECK_STATUS(outcome, Status::invalid_state);
  CHECK_EQ(fixture.answers().taken(), 1u);
  CHECK_EQ(fixture.peer().queued(), 1u);
  CHECK_EQ(fixture.host().live_descriptors(), 0u); }

TEST(renderer_link_drain, hands_every_answer_already_there_to_the_late_taker_and_closes_what_is_not_its)
{ Fixture fixture;
  fixture.answers().claim(Kind::exported_image, Status::ok);
  fixture.queue_late(Kind::exported_image, 7u, 1u, 1u);
  fixture.queue_late(Kind::reply, 3u, 0u, 2u);
  const std::uint8_t garbage[3] = {0xFFu, 0xFFu, 0xFFu};
  fixture.peer().queue(garbage, sizeof garbage, 1u);
  fixture.queue_late(Kind::exported_image, 8u, 2u, 0u);
  CHECK_STATUS(fixture.link().drain(), Status::ok);
  CHECK_EQ(fixture.answers().taken(), 2u);
  CHECK_EQ(fixture.answers().sequence_at(0u), 7u);
  CHECK_EQ(fixture.answers().sequence_at(1u), 8u);
  CHECK_EQ(fixture.peer().queued(), 0u);
  CHECK_EQ(fixture.host().live_descriptors(), 0u);
  CHECK_EQ(fixture.host().last_receive_timeout(), 0u);
  CHECK_EQ(fixture.peer().delivered(), 0u);
  CHECK_STATUS(fixture.link().drain(), Status::ok); }

TEST(renderer_link_drain, stops_at_the_first_answer_the_taker_finds_bad_or_a_renderer_gone)
{ { Fixture fixture;
    fixture.answers().claim(Kind::exported_image, Status::invalid_state);
    fixture.queue_late(Kind::exported_image, 7u, 1u, 0u);
    fixture.queue_late(Kind::exported_image, 8u, 1u, 0u);
    CHECK_STATUS(fixture.link().drain(), Status::invalid_state);
    CHECK_EQ(fixture.answers().taken(), 1u);
    CHECK_EQ(fixture.peer().queued(), 1u); }
  Fixture fixture;
  fixture.host().renderer_gone();
  CHECK_STATUS(fixture.link().drain(), Status::not_found);
  CHECK_EQ(fixture.answers().taken(), 0u); }

TEST(renderer_link_drain, takes_no_more_than_its_bound_from_a_renderer_that_never_stops_talking)
{ MockHost                 host;
  EndlessPeer              peer;
  av::RecordingLateAnswers answers;
  av::LateAnswers         &taker = answers;
  RendererLink             link(host, taker);
  host.connect_renderer(peer);
  CHECK_STATUS(host.start_renderer("av-renderer"), Status::ok);
  answers.claim(Kind::exported_image, Status::ok);
  CHECK_STATUS(link.drain(), Status::ok);
  CHECK_EQ(peer.answered(), RendererLink::max_drained);
  CHECK_EQ(answers.taken(), av::RecordingLateAnswers::max_taken); }
