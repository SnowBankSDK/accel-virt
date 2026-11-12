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
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.active());
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, 0u) == ReleaseQueue::Turn::release_now); }

TEST(release_queue, a_batch_that_comes_first_waits_for_the_one_before_it)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK_EQ(queue.held(), 2u);
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
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
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.active()); }

TEST(release_queue, a_batch_seen_again_is_done_or_held_once)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::released_already);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK_EQ(queue.held(), 1u); }

TEST(release_queue, an_overlap_and_a_full_queue_are_refused)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(2u, 32u, 96u), 0u, 0u) == ReleaseQueue::Turn::refused);
  for (std::uint32_t index = 0u; index < ReleaseQueue::most_held; index++)
      CHECK(queue.arrive(batch(10u + index, 1000u + 64u * index, 1064u + 64u * index), 0u, 0u) ==
            ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(999u, 100000u, 100064u), 0u, 0u) == ReleaseQueue::Turn::refused); }

TEST(release_queue, clear_forgets_what_was_held_and_begins_an_order_again)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, 0u) == ReleaseQueue::Turn::held);
  queue.clear();
  CHECK(!queue.active());
  CHECK_EQ(queue.held(), 0u);
  CHECK(queue.arrive(batch(5u, 500u, 564u), 500u, 0u) == ReleaseQueue::Turn::release_now); }

TEST(release_queue, the_next_held_is_found_past_batches_that_are_not_next)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
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
  CHECK(queue.arrive(batch(1u, 96u, 160u), 96u, 0u) == ReleaseQueue::Turn::release_now);
  CHECK_EQ(queue.next(), 96u);
  queue.released(batch(1u, 96u, 160u));
  CHECK_EQ(queue.next(), 160u); }

TEST(release_queue, an_order_left_behind_by_batches_the_process_published_itself_begins_again_where_they_end)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(4u, 192u, 256u), 192u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(4u, 192u, 256u));
  CHECK(queue.arrive(batch(6u, 320u, 384u), 384u, 0u) == ReleaseQueue::Turn::release_now);
  CHECK_EQ(queue.next(), 320u);
  queue.released(batch(6u, 320u, 384u));
  CHECK(queue.arrive(batch(5u, 256u, 320u), 384u, 0u) == ReleaseQueue::Turn::released_already);
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue, a_batch_past_what_the_process_published_still_waits_and_one_the_publication_passed_is_dropped)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(3u, 128u, 192u), 64u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(5u, 256u, 320u), 100u, 0u) == ReleaseQueue::Turn::held);
  CHECK_EQ(queue.next(), 100u);
  CHECK_EQ(queue.held(), 2u);
  CHECK(queue.arrive(batch(4u, 192u, 256u), 192u, 0u) == ReleaseQueue::Turn::release_now);
  CHECK_EQ(queue.held(), 1u);
  queue.released(batch(4u, 192u, 256u));
  SubmissionDescriptor next;
  CHECK(queue.next_held(next));
  CHECK(next.fence_id() == 5u);
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue, a_batch_released_while_a_copy_of_it_was_held_leaves_nothing_held)
{ ReleaseQueue queue;
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  CHECK(queue.arrive(batch(3u, 128u, 192u), 64u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 128u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(3u, 128u, 192u));
  SubmissionDescriptor next;
  CHECK(!queue.next_held(next));
  CHECK_EQ(queue.held(), 0u); }

TEST(release_queue, held_batches_are_stranded_only_once_the_writer_of_the_batch_before_the_first_is_gone)
{ ReleaseQueue queue;
  int first_writer = 0;
  int second_writer = 0;
  CHECK(!queue.stranded());
  queue.written_by(1u, &first_writer);
  queue.written_by(2u, &second_writer);
  queue.written_by(0u, &second_writer);
  queue.written_by(3u, nullptr);
  CHECK(queue.arrive(batch(3u, 128u, 192u), 0u, 1u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, 2u) == ReleaseQueue::Turn::held);
  CHECK(!queue.stranded());
  CHECK(queue.forget(&first_writer));
  CHECK(!queue.forget(&first_writer));
  CHECK(!queue.forget(nullptr));
  CHECK(!queue.stranded());
  CHECK(queue.forget(&second_writer));
  CHECK(queue.stranded());
  queue.clear();
  CHECK(!queue.stranded()); }

TEST(release_queue, a_writer_never_seen_or_pushed_out_of_a_full_table_is_never_taken_for_gone)
{ ReleaseQueue queue;
  int writers[ReleaseQueue::most_writers + 1u] = {};
  for (std::uint32_t index = 0u; index < ReleaseQueue::most_writers; index++)
      queue.written_by(index + 1u, &writers[index]);
  queue.written_by(1u, &writers[ReleaseQueue::most_writers]);
  queue.written_by(ReleaseQueue::most_writers + 1u, &writers[ReleaseQueue::most_writers]);
  CHECK(queue.arrive(batch(2u, 64u, 128u), 0u, ReleaseQueue::most_writers + 1u) == ReleaseQueue::Turn::held);
  CHECK(!queue.forget(&writers[ReleaseQueue::most_writers]));
  CHECK(!queue.stranded());
  CHECK(queue.forget(&writers[0]));
  queue.written_by(ReleaseQueue::most_writers + 1u, &writers[ReleaseQueue::most_writers]);
  CHECK(queue.forget(&writers[ReleaseQueue::most_writers]));
  CHECK(queue.stranded());
  CHECK(queue.arrive(batch(1u, 0u, 64u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 64u));
  SubmissionDescriptor next;
  CHECK(queue.next_held(next));
  queue.released(next);
  CHECK(!queue.stranded());
  CHECK(queue.arrive(batch(4u, 192u, 256u), 0u, 99u) == ReleaseQueue::Turn::held);
  CHECK(!queue.stranded()); }

TEST(release_queue_next_forced, takes_the_held_batches_in_ring_order_each_widened_back_to_where_the_order_stands)
{ ReleaseQueue queue;
  SubmissionDescriptor next;
  CHECK(!queue.next_forced(next));
  CHECK(queue.arrive(batch(1u, 0u, 32u), 0u, 0u) == ReleaseQueue::Turn::release_now);
  queue.released(batch(1u, 0u, 32u));
  CHECK(queue.arrive(batch(5u, 160u, 192u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.arrive(batch(3u, 64u, 96u), 0u, 0u) == ReleaseQueue::Turn::held);
  CHECK(queue.next_forced(next));
  CHECK_EQ(next.fence_id(), 3u);
  CHECK_EQ(next.begin(), 32u);
  CHECK_EQ(next.end(), 96u);
  queue.released(next);
  CHECK(queue.next_forced(next));
  CHECK_EQ(next.fence_id(), 5u);
  CHECK_EQ(next.begin(), 96u);
  CHECK_EQ(next.end(), 192u);
  queue.released(next);
  CHECK(!queue.next_forced(next));
  CHECK_EQ(queue.held(), 0u);
  CHECK_EQ(queue.next(), 192u);
  CHECK(queue.arrive(batch(2u, 32u, 64u), 0u, 0u) == ReleaseQueue::Turn::released_already);
  CHECK(queue.arrive(batch(4u, 96u, 160u), 0u, 0u) == ReleaseQueue::Turn::released_already);
  CHECK(queue.arrive(batch(6u, 192u, 224u), 0u, 0u) == ReleaseQueue::Turn::release_now); }
