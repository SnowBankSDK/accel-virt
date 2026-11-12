#include "core/support/status.hpp"
#include "harness/framework/byte_layout.hpp"
#include "harness/framework/guarded_buffer.hpp"
#include "harness/framework/hostile_sweep.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/submission/lease_record.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

using av::ByteLayout;
using av::GuardedBuffer;
using av::LeaseRecord;
using av::Status;

namespace {

bool rejects(const std::uint8_t *bytes, std::size_t length)
{
    LeaseRecord  record;
    const Status status = record.decode(bytes, length);
    if (status == Status::ok) {
        std::uint8_t again[LeaseRecord::encoded_size] = {};
        if (record.encode(again, sizeof again) != Status::ok || std::memcmp(again, bytes, sizeof again) != 0)
            av::TestRegistry::instance().record_failure(__FILE__, __LINE__, "accepted input did not round trip");
    }
    return status != Status::ok;
}

}

TEST(lease_record_encode, lays_out_kind_reserved_word_bytes_and_least)
{ std::uint8_t bytes[LeaseRecord::encoded_size];
  std::memset(bytes, 0xEE, sizeof bytes);
  CHECK_STATUS(LeaseRecord(LeaseRecord::Kind::host, 0x1122334455667788u, 0x0102u).encode(bytes, sizeof bytes),
               Status::ok);
  const ByteLayout layout(bytes, sizeof bytes);
  CHECK(layout.at(0u, 4u) == 1u);
  CHECK(layout.at(4u, 4u) == 0u);
  CHECK(layout.at(8u, 8u) == 0x1122334455667788u);
  CHECK(layout.at(16u, 8u) == 0x0102u); }

TEST(lease_record_encode, a_short_destination_is_too_large_and_untouched)
{ GuardedBuffer buffer(LeaseRecord::encoded_size - 1u);
  CHECK_STATUS(LeaseRecord(LeaseRecord::Kind::device, 1u, 1u).encode(buffer.data(), buffer.size()), Status::too_large);
  CHECK(buffer.guards_intact()); }

TEST(lease_record_encode, a_least_above_the_asked_bytes_is_not_written)
{ std::uint8_t bytes[LeaseRecord::encoded_size] = {};
  CHECK_STATUS(LeaseRecord(LeaseRecord::Kind::device, 1u, 2u).encode(bytes, sizeof bytes), Status::invalid_parameter); }

TEST(lease_record_decode, every_kind_round_trips_its_sizes)
{ const LeaseRecord::Kind kinds[] = {LeaseRecord::Kind::device, LeaseRecord::Kind::host};
  for (const LeaseRecord::Kind kind : kinds) {
      std::uint8_t bytes[LeaseRecord::encoded_size] = {};
      CHECK_STATUS(LeaseRecord(kind, 64u << 20, 3u << 20).encode(bytes, sizeof bytes), Status::ok);
      LeaseRecord decoded;
      CHECK_STATUS(decoded.decode(bytes, sizeof bytes), Status::ok);
      CHECK(decoded.kind() == kind);
      CHECK_EQ(decoded.bytes(), 64u << 20);
      CHECK_EQ(decoded.least(), 3u << 20);
  } }

TEST(lease_record_decode, an_unknown_kind_a_reserved_word_or_least_above_bytes_is_malformed_and_left_unread)
{ std::uint8_t bytes[LeaseRecord::encoded_size] = {};
  CHECK_STATUS(LeaseRecord(LeaseRecord::Kind::device, 8u, 4u).encode(bytes, sizeof bytes), Status::ok);
  LeaseRecord decoded(LeaseRecord::Kind::host, 5u, 5u);
  std::uint8_t unknown[sizeof bytes];
  std::memcpy(unknown, bytes, sizeof bytes);
  unknown[0] = static_cast<std::uint8_t>(LeaseRecord::kinds);
  CHECK_STATUS(decoded.decode(unknown, sizeof unknown), Status::malformed);
  std::uint8_t reserved[sizeof bytes];
  std::memcpy(reserved, bytes, sizeof bytes);
  reserved[5] = 1u;
  CHECK_STATUS(decoded.decode(reserved, sizeof reserved), Status::malformed);
  std::uint8_t inverted[sizeof bytes];
  std::memcpy(inverted, bytes, sizeof bytes);
  inverted[16] = 9u;
  CHECK_STATUS(decoded.decode(inverted, sizeof inverted), Status::malformed);
  CHECK(decoded.kind() == LeaseRecord::Kind::host);
  CHECK_EQ(decoded.bytes(), 5u); }

TEST(lease_record_decode, any_length_but_the_record_is_malformed)
{ std::uint8_t bytes[LeaseRecord::encoded_size + 1u] = {};
  CHECK_STATUS(LeaseRecord(LeaseRecord::Kind::device, 1u, 0u).encode(bytes, sizeof bytes), Status::ok);
  for (std::size_t length = 0u; length <= sizeof bytes; length++) {
      if (length == LeaseRecord::encoded_size) continue;
      LeaseRecord decoded;
      CHECK_STATUS(decoded.decode(bytes, length), Status::malformed);
  } }

TEST(lease_record_decode, survives_the_hostile_sweep)
{ std::uint8_t bytes[LeaseRecord::encoded_size] = {};
  CHECK_STATUS(LeaseRecord(LeaseRecord::Kind::host, 0x4000u, 0x1000u).encode(bytes, sizeof bytes), Status::ok);
  const av::HostileSweep sweep(rejects, bytes, sizeof bytes);
  sweep.run();
  CHECK(sweep.cases_run() > 128u);
  CHECK(sweep.rejections() > 0u); }
