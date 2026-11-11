#include "harness/framework/test_registry.hpp"
#include "icd/d3d/completion_watermark.hpp"

#include <cstdint>

using av::d3d::CompletionWatermark;

TEST(completion_watermark, writes_before_an_event_end_are_done_once_the_event_is_seen_and_later_writes_are_not)
{ CompletionWatermark watermark;
  const std::uint64_t first = watermark.stamp_write();
  const std::uint64_t second = watermark.stamp_write();
  const std::uint64_t event = watermark.ended();
  const std::uint64_t later = watermark.stamp_write();
  CHECK(!watermark.known_done(first));
  watermark.seen(event);
  CHECK(watermark.known_done(first));
  CHECK(watermark.known_done(second));
  CHECK(!watermark.known_done(later));
  CHECK(!watermark.known_done(0u)); }

TEST(completion_watermark, an_older_event_read_after_a_newer_one_keeps_the_highest_watermark)
{ CompletionWatermark watermark;
  const std::uint64_t first = watermark.stamp_write();
  const std::uint64_t older = watermark.ended();
  const std::uint64_t second = watermark.stamp_write();
  const std::uint64_t newer = watermark.ended();
  watermark.seen(newer);
  watermark.seen(older);
  CHECK(watermark.known_done(first));
  CHECK(watermark.known_done(second)); }

TEST(completion_watermark, an_event_ended_before_any_write_marks_nothing_done)
{ CompletionWatermark watermark;
  const std::uint64_t event = watermark.ended();
  watermark.seen(event);
  CHECK(!watermark.known_done(watermark.stamp_write())); }
