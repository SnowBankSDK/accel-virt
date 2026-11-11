#include "harness/framework/test_registry.hpp"
#include "miniport/av_context/release_queue.hpp"
#include "proto/submission/submission_descriptor.hpp"

#include <cstdint>

using av::ReleaseQueue;
using av::SubmissionDescriptor;

namespace {

SubmissionDescriptor batch(std::uint64_t fence, std::uint64_t begin, std::uint64_t end)
{ return SubmissionDescriptor(fence, begin, end); }

}

TEST(release_queue, batches_in_order_are_released_as_they_arrive)
{ ReleaseQueue queue;
  CHECK(!queue.active());
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.active());
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u) == ReleaseQueue::Turn::release_now); }

TEST(release_queue, a_batch_that_comes_first_waits_for_the_one_before_it)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u) == ReleaseQueue::Turn::held);
  CHECK_EQ(queue.held(), 2u);
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  SubmissionDescriptor next;
  CHECK(queue.next_held(next));
  CHECK(next.fence_id() == 2u);
  queue.released(next);
  CHECK(queue.next_held(next));
  CHECK(next.fence_id() == 3u);
  queue.released(next);
  CHECK(!queue.next_held(next));
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue, the_order_begins_at_the_tail_so_an_earlier_unpublished_batch_holds_the_first_release)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.active()); }

TEST(release_queue, a_batch_seen_again_is_done_or_held_once)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::released_already);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u) == ReleaseQueue::Turn::held);
  CHECK_EQ(queue.held(), 1u); }

TEST(release_queue, an_overlap_and_a_full_queue_are_refused)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(2u, 32u, 96u), 0u) == ReleaseQueue::Turn::refused);
  for (std::uint32_t index = 0u; index < ReleaseQueue::most_held; index++)
      CHECK(queue.arrive(batch(10u + index, 1000u + 64u * index, 1064u + 64u * index), 0u) ==
            ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(999u, 100000u, 100064u), 0u) == ReleaseQueue::Turn::refused); }

TEST(release_queue, clear_forgets_what_was_held_and_begins_an_order_again)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u) == ReleaseQueue::Turn::held);
  queue.clear();
  CHECK(!queue.active());
  CHECK_EQ(queue.held(), 0u);
  CHECK(queue.arrive(batch(5u, 500u, 564u), 500u) == ReleaseQueue::Turn::release_now); }

TEST(release_queue, the_next_held_is_found_past_batches_that_are_not_next)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  SubmissionDescriptor next;
  CHECK(queue.next_held(next));
  CHECK(next.fence_id() == 2u);
  queue.released(next);
  CHECK(queue.next_held(next));
  CHECK(next.fence_id() == 3u);
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue_next, is_where_the_next_batch_released_begins)
{ ReleaseQueue queue;
  CHECK_EQ(queue.next(), 0u);
  CHECK(queue.arrive(batch(1u, 96u, 160u), 96u) == ReleaseQueue::Turn::release_now);
  CHECK_EQ(queue.next(), 96u);
  queue.released(batch(1u, 96u, 160u));
  CHECK_EQ(queue.next(), 160u); }

TEST(release_queue, an_order_left_behind_by_batches_the_process_published_itself_begins_again_where_they_end)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(4u, 192u, 256u), 192u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(4u, 192u, 256u));
  CHECK(queue.arrive(batch(6u, 320u, 384u), 384u) == ReleaseQueue::Turn::release_now);
  CHECK_EQ(queue.next(), 320u);
  queue.released(batch(6u, 320u, 384u));
  CHECK(queue.arrive(batch(5u, 256u, 320u), 384u) == ReleaseQueue::Turn::released_already);
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue, a_batch_past_what_the_process_published_still_waits_and_one_the_publication_passed_is_dropped)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(3u, 128u, 192u), 64u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(5u, 256u, 320u), 100u) == ReleaseQueue::Turn::held);
  CHECK_EQ(queue.next(), 100u);
  CHECK_EQ(queue.held(), 2u);
  CHECK(queue.arrive(batch(4u, 192u, 256u), 192u) == ReleaseQueue::Turn::release_now);
  CHECK_EQ(queue.held(), 1u);
  queue.released(batch(4u, 192u, 256u));
  SubmissionDescriptor next;
  CHECK(queue.next_held(next));
  CHECK(next.fence_id() == 5u);
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue, a_batch_released_while_a_copy_of_it_was_held_leaves_nothing_held)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(3u, 128u, 192u), 64u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 128u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(3u, 128u, 192u));
  SubmissionDescriptor next;
  CHECK(!queue.next_held(next));
  CHECK_EQ(queue.held(), 0u); }
