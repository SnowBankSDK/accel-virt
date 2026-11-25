#include "core/support/status.hpp"
#include "harness/framework/byte_layout.hpp"
#include "harness/framework/guarded_buffer.hpp"
#include "harness/framework/hostile_sweep.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/renderer/export_image_message.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

using av::ByteLayout;
using av::ExportImageMessage;
using av::GuardedBuffer;
using av::Status;

namespace {

bool rejects(const std::uint8_t *bytes, std::size_t length)
{
    ExportImageMessage message;
    const Status       status = message.decode(bytes, length, ExportImageMessage::descriptors);
    if (status == Status::ok) {
        std::uint8_t again[ExportImageMessage::encoded_size] = {};
        if (message.encode(again, sizeof again) != Status::ok || std::memcmp(again, bytes, sizeof again) != 0)
            av::TestRegistry::instance().record_failure(__FILE__, __LINE__, "accepted input did not round trip");
    }
    return status != Status::ok;
}

}

TEST(export_image_message_encode, is_the_key_then_the_head_then_a_zero_word)
{ GuardedBuffer bytes(ExportImageMessage::encoded_size);
  CHECK_EQ(ExportImageMessage::encoded_size, 16u);
  CHECK_STATUS(ExportImageMessage(0x1122334455667788u, 3u).encode(bytes.data(), bytes.size()), Status::ok);
  const ByteLayout layout(bytes.data(), bytes.size());
  CHECK(layout.at(0u, 8u) == 0x1122334455667788u);
  CHECK(layout.at(8u, 4u) == 3u);
  CHECK(layout.at(12u, 4u) == 0u);
  CHECK_EQ(ExportImageMessage::descriptors, 0u);
  CHECK(bytes.guards_intact()); }

TEST(export_image_message, round_trips_every_key_and_head_through_its_encoding)
{ const std::uint64_t keys[] = {0u, 1u, 0xFFFFFFFFFFFFFFFFu};
  const std::uint32_t heads[] = {0u, 1u, 15u};
  for (const std::uint64_t key : keys)
      for (const std::uint32_t head : heads) {
          std::uint8_t bytes[ExportImageMessage::encoded_size] = {};
          CHECK_STATUS(ExportImageMessage(key, head).encode(bytes, sizeof bytes), Status::ok);
          ExportImageMessage decoded(0x55u, 7u);
          CHECK_STATUS(decoded.decode(bytes, sizeof bytes, 0u), Status::ok);
          CHECK_EQ(decoded.key(), key);
          CHECK_EQ(decoded.head(), head);
      }
  CHECK_EQ(ExportImageMessage().key(), 0u);
  CHECK_EQ(ExportImageMessage().head(), 0u); }

TEST(export_image_message, a_head_past_the_last_is_neither_encoded_nor_decoded_and_the_last_head_is)
{ std::uint8_t bytes[ExportImageMessage::encoded_size] = {};
  CHECK_EQ(ExportImageMessage::heads, 16u);
  CHECK_STATUS(ExportImageMessage(9u, 16u).encode(bytes, sizeof bytes), Status::invalid_parameter);
  CHECK_STATUS(ExportImageMessage(9u, 15u).encode(bytes, sizeof bytes), Status::ok);
  ExportImageMessage decoded(4u, 2u);
  CHECK_STATUS(decoded.decode(bytes, sizeof bytes, 0u), Status::ok);
  CHECK_EQ(decoded.head(), 15u);
  bytes[8] = 16u;
  CHECK_STATUS(decoded.decode(bytes, sizeof bytes, 0u), Status::malformed);
  bytes[8] = 15u;
  bytes[12] = 1u;
  CHECK_STATUS(decoded.decode(bytes, sizeof bytes, 0u), Status::malformed);
  CHECK_EQ(decoded.key(), 9u);
  CHECK_EQ(decoded.head(), 15u); }

TEST(export_image_message_encode, a_short_or_missing_destination_is_too_large)
{ GuardedBuffer buffer(ExportImageMessage::encoded_size - 1u);
  CHECK_STATUS(ExportImageMessage(1u, 0u).encode(buffer.data(), buffer.size()), Status::too_large);
  CHECK(buffer.guards_intact());
  CHECK_STATUS(ExportImageMessage(1u, 0u).encode(nullptr, ExportImageMessage::encoded_size), Status::too_large); }

TEST(export_image_message_decode, any_descriptor_other_length_or_no_bytes_is_malformed_and_changes_nothing)
{ std::uint8_t bytes[ExportImageMessage::encoded_size + 1u] = {};
  CHECK_STATUS(ExportImageMessage(9u, 1u).encode(bytes, sizeof bytes), Status::ok);
  ExportImageMessage decoded(4u, 2u);
  CHECK_STATUS(decoded.decode(bytes, ExportImageMessage::encoded_size, 1u), Status::malformed);
  CHECK_STATUS(decoded.decode(bytes, ExportImageMessage::encoded_size - 1u, 0u), Status::malformed);
  CHECK_STATUS(decoded.decode(bytes, ExportImageMessage::encoded_size + 1u, 0u), Status::malformed);
  CHECK_STATUS(decoded.decode(bytes, 0u, 0u), Status::malformed);
  CHECK_STATUS(decoded.decode(nullptr, ExportImageMessage::encoded_size, 0u), Status::malformed);
  CHECK_EQ(decoded.key(), 4u);
  CHECK_EQ(decoded.head(), 2u); }

TEST(export_image_message_decode, survives_the_hostile_sweep)
{ std::uint8_t bytes[ExportImageMessage::encoded_size] = {};
  CHECK_STATUS(ExportImageMessage(3u, 1u).encode(bytes, sizeof bytes), Status::ok);
  const av::HostileSweep sweep(rejects, bytes, sizeof bytes);
  sweep.run();
  CHECK(sweep.cases_run() > 64u);
  CHECK(sweep.rejections() >= ExportImageMessage::encoded_size); }
