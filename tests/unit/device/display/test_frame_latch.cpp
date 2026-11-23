#include "device/display/frame_latch.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstdint>

namespace av {

struct FrameLatchProbe {
    static void claim_unshown(FrameLatch &latch) { latch.unshown_ = true; }
    static void set_pending_key(FrameLatch &latch, std::uint64_t key) { latch.pending_key_ = key; }
    static void set_pending_exported(FrameLatch &latch) { latch.pending_exported_ = true; }
    static void set_counts(FrameLatch &latch, std::uint64_t posts, std::uint64_t exported, std::uint64_t dropped)
    {
        latch.posts_ = posts;
        latch.posts_exported_on_post_ = exported;
        latch.posts_dropped_ = dropped;
    }
};

}

using av::FrameLatch;
using av::FrameLatchProbe;

TEST(frame_latch, a_post_waits_for_the_blank)
{ FrameLatch latch;
  std::uint64_t key = 0u;
  CHECK(!latch.latch_to_show());
  CHECK(!latch.take_unshown(key));
  latch.post(1u, 0x11u);
  CHECK(latch.invariants_hold());
  CHECK_EQ(latch.latched(), 0u);
  CHECK(!latch.take_unshown(key));
  CHECK(latch.latch_to_show());
  CHECK_EQ(latch.latched(), 1u);
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, the_newest_post_before_a_blank_is_the_one_shown)
{ FrameLatch latch;
  latch.post(1u, 0x11u);
  latch.post(2u, 0x22u);
  latch.post(3u, 0x33u);
  CHECK(latch.latch_to_show());
  CHECK_EQ(latch.latched(), 3u);
  std::uint64_t key = 0u;
  CHECK(latch.take_unshown(key));
  CHECK_EQ(key, 0x33u);
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, a_blank_with_nothing_new_keeps_the_frame_and_shows_it_no_second_time)
{ FrameLatch latch;
  latch.post(4u, 0x44u);
  CHECK(latch.latch_to_show());
  std::uint64_t key = 0u;
  CHECK(latch.take_unshown(key));
  CHECK(!latch.latch_to_show());
  CHECK_EQ(latch.latched(), 4u);
  CHECK(!latch.take_unshown(key));
  latch.post(4u, 0x44u);
  CHECK(!latch.latch_to_show());
  CHECK(!latch.take_unshown(key));
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, frames_that_wrapped_are_still_new_and_frame_zero_is_none)
{ FrameLatch latch;
  latch.post(0xFFFFFFFFu, 0x10u);
  CHECK(latch.latch_to_show());
  latch.post(0u, 0x20u);
  CHECK(!latch.latch_to_show());
  CHECK_EQ(latch.latched(), 0xFFFFFFFFu);
  latch.post(1u, 0x30u);
  CHECK(latch.latch_to_show());
  CHECK_EQ(latch.latched(), 1u);
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, a_frame_latched_and_not_yet_shown_is_shown_as_the_newest)
{ FrameLatch latch;
  latch.post(1u, 0x11u);
  CHECK(latch.latch_to_show());
  latch.post(2u, 0x22u);
  CHECK(latch.latch_to_show());
  std::uint64_t key = 0u;
  CHECK(latch.take_unshown(key));
  CHECK_EQ(key, 0x22u);
  CHECK(!latch.take_unshown(key)); }

TEST(frame_latch, a_reset_forgets_what_was_posted_latched_and_waiting)
{ FrameLatch latch;
  latch.post(5u, 0x55u);
  CHECK(latch.latch_to_show());
  latch.post(6u, 0x66u);
  latch.clear();
  CHECK(latch.invariants_hold());
  CHECK_EQ(latch.latched(), 0u);
  std::uint64_t key = 0u;
  CHECK(!latch.take_unshown(key));
  CHECK(!latch.latch_to_show());
  latch.post(5u, 0x55u);
  CHECK(latch.latch_to_show()); }

TEST(frame_latch, an_unshown_frame_nothing_was_latched_for_or_a_key_with_no_frame_breaks_the_invariant)
{ FrameLatch unshown;
  FrameLatchProbe::claim_unshown(unshown);
  CHECK(!unshown.invariants_hold());
  FrameLatch keyed;
  FrameLatchProbe::set_pending_key(keyed, 0x77u);
  CHECK(!keyed.invariants_hold());
  keyed.post(1u, 0x77u);
  CHECK(keyed.invariants_hold()); }

TEST(frame_latch, a_frame_exported_when_posted_is_latched_for_the_guest_and_not_shown_again)
{ FrameLatch latch;
  latch.post_exported(0u, 0x10u);
  CHECK_EQ(latch.posts(), 0u);
  latch.post_exported(1u, 0x11u);
  CHECK(latch.invariants_hold());
  CHECK(!latch.latch_to_show());
  CHECK_EQ(latch.latched(), 1u);
  std::uint64_t key = 0u;
  CHECK(!latch.take_unshown(key));
  CHECK(!latch.latch_to_show());
  latch.post(2u, 0x22u);
  CHECK(latch.latch_to_show());
  CHECK(latch.take_unshown(key));
  CHECK_EQ(key, 0x22u);
  CHECK_EQ(latch.posts(), 2u);
  CHECK_EQ(latch.posts_exported_on_post(), 1u);
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, a_frame_exported_when_posted_takes_over_from_one_latched_and_not_yet_shown)
{ FrameLatch latch;
  latch.post(1u, 0x11u);
  CHECK(latch.latch_to_show());
  latch.post_exported(2u, 0x22u);
  CHECK(!latch.latch_to_show());
  CHECK_EQ(latch.latched(), 2u);
  std::uint64_t key = 0u;
  CHECK(!latch.take_unshown(key));
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, only_a_post_replaced_before_its_blank_without_an_export_is_counted_dropped)
{ FrameLatch latch;
  latch.post(1u, 0x11u);
  latch.post(2u, 0x22u);
  CHECK_EQ(latch.posts_dropped(), 1u);
  CHECK(latch.latch_to_show());
  latch.post(3u, 0x33u);
  CHECK_EQ(latch.posts_dropped(), 1u);
  latch.post_exported(4u, 0x44u);
  CHECK_EQ(latch.posts_dropped(), 2u);
  latch.post(5u, 0x55u);
  CHECK_EQ(latch.posts_dropped(), 2u);
  CHECK_EQ(latch.posts(), 5u);
  CHECK_EQ(latch.posts_exported_on_post(), 1u);
  CHECK(latch.invariants_hold()); }

TEST(frame_latch, a_reset_forgets_an_export_made_at_the_post_and_keeps_the_counts)
{ FrameLatch latch;
  latch.post_exported(1u, 0x11u);
  latch.clear();
  CHECK(latch.invariants_hold());
  latch.post(1u, 0x11u);
  CHECK(latch.latch_to_show());
  std::uint64_t key = 0u;
  CHECK(latch.take_unshown(key));
  CHECK_EQ(latch.posts(), 2u);
  CHECK_EQ(latch.posts_exported_on_post(), 1u); }

TEST(frame_latch, an_export_with_no_frame_or_counts_past_the_posts_break_the_invariant)
{ FrameLatch exported;
  FrameLatchProbe::set_pending_exported(exported);
  CHECK(!exported.invariants_hold());
  FrameLatch overcounted;
  FrameLatchProbe::set_counts(overcounted, 1u, 2u, 0u);
  CHECK(!overcounted.invariants_hold());
  FrameLatch overdropped;
  FrameLatchProbe::set_counts(overdropped, 1u, 0u, 2u);
  CHECK(!overdropped.invariants_hold());
  FrameLatchProbe::set_counts(overdropped, 2u, 1u, 2u);
  CHECK(overdropped.invariants_hold()); }
