#include "core/log/driver_log.hpp"
#include "core/log/keyed_log.hpp"
#include "guest/kmt/avgpu_adapters.hpp"
#include "guest/suite/adapter_scenario.hpp"
#include "harness/guest/mock_kmt.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/icd/counted_wake_signal.hpp"
#include "harness/icd/lock_trace.hpp"
#include "harness/icd/paced_display.hpp"
#include "harness/icd/recording_log.hpp"
#include "harness/icd/recording_thread_lock.hpp"
#include "harness/icd/scripted_thread.hpp"
#include "harness/icd/scripted_window_chains.hpp"
#include "harness/icd/scripted_window_surface.hpp"
#include "harness/platform/mock_platform.hpp"
#include "icd/common/frame_wait.hpp"
#include "icd/common/present_pacing.hpp"
#include "icd/common/window_present_queue.hpp"

#include <atomic>
#include <cstdint>

using av::icd::FrameWait;

namespace {

struct Mark {
    std::uint32_t id = 0u;
};

class Readiness {
public:
    using Mark = ::Mark;

    FrameWait wait(const Mark &mark)
    {
        waited_ = mark.id;
        return answer_;
    }

    void          answer(FrameWait answer) { answer_ = answer; }
    std::uint32_t waited() const { return waited_; }

private:
    FrameWait     answer_ = FrameWait::drawn;
    std::uint32_t waited_ = 0u;
};

class Owner;

using Queue = av::icd::WindowPresentQueue<Owner, Readiness>;
using Frame = Queue::Frame;

class Owner {
public:
    void done(const Frame &frame, bool drawn)
    {
        finished_++;
        last_slot_ = frame.slot();
        last_drawn_ = drawn;
    }

    std::uint32_t finished() const { return finished_; }
    std::uint32_t last_slot() const { return last_slot_; }
    bool          last_drawn() const { return last_drawn_; }
    bool          lost() const { return lost_.load(); }
    void          lose() { lost_ = true; }

private:
    std::atomic<bool> lost_{false};
    std::uint32_t finished_ = 0u;
    std::uint32_t last_slot_ = 0u;
    bool          last_drawn_ = false;
};

class Keeper final : public av::icd::HeldFlusher {
public:
    std::uint32_t flush_held() override
    {
        flushed_++;
        return later_;
    }

    void          later(std::uint32_t milliseconds) { later_ = milliseconds; }
    std::uint32_t flushed() const { return flushed_; }

private:
    std::uint32_t later_ = 0u;
    std::uint32_t flushed_ = 0u;
};

struct Rig {
    av::MockPlatform          platform;
    av::RecordingLog          log;
    av::KeyedLog              lines{platform, log, "avtest: "};
    av::ScriptedThread        thread;
    av::CountedWakeSignal     handed;
    av::CountedWakeSignal     shown;
    av::LockTrace             trace;
    av::RecordingThreadLock   lock{trace, 'P'};
    av::ScriptedWindowSurface surface;
    av::ScriptedWindowChains  chains;
    Readiness                 readiness;
    Owner                     owner;
    Queue                     queue{platform, thread, handed, shown, lock, readiness, surface, chains, lines};
    int                       dc = 0;
    int                       window = 0;
    std::uint8_t              pixels[4 * 2 * 4] = {};

    Rig() { surface.add_dc(&dc, &window, 4u, 2u); }

    Frame frame(std::uint32_t slot, std::uint32_t mark = 1u)
    {
        return Frame(owner, slot, pixels, Frame::Rows::bgra_bottom_up, 4u, 2u, &window, Mark{mark});
    }
};

}

namespace av {
struct GrowableArrayProbe {
    template <typename T>
    static void corrupt_capacity(GrowableArray<T> &array, std::uint32_t capacity) { array.capacity_ = capacity; }
};

}

namespace av::icd {
struct WindowPresentQueueProbe {
    static void add_row(Queue &queue, Owner &owner, bool held, std::uint64_t order)
    { const Queue::Row row{held ? Frame(owner, 0u, nullptr, Frame::Rows::bgra_bottom_up, 1u, 1u, nullptr, Mark{})
                                : Frame(),
                           held ? Queue::Stage::queued : Queue::Stage::free, order};
      CHECK_STATUS(queue.rows_.append(row), av::Status::ok); }
    static void clear_rows(Queue &queue) { queue.rows_.clear(); }
    static void set_next_order(Queue &queue, std::uint64_t order) { queue.next_order_ = order; }
    static void set_scratch_bytes(Queue &queue, std::size_t bytes) { queue.scratch_bytes_ = bytes; }
    static void corrupt_rows(Queue &queue) { av::GrowableArrayProbe::corrupt_capacity(queue.rows_, 1u); }
};

}

using av::icd::WindowPresentQueueProbe;

TEST(window_present_queue, a_fifo_frame_with_a_free_slot_is_handed_without_waiting_and_the_thread_shows_one_frame_per_blank)
{ Rig          rig;
  av::PacedDisplay display;
  CHECK(rig.queue.start());
  for (std::uint32_t slot = 0u; slot < 2u; slot++) {
      CHECK_EQ(rig.queue.try_slot(rig.owner, 2u, 2u), slot);
      Frame frame = rig.frame(slot, slot + 1u);
      frame.paced_by(&display.pacing);
      CHECK(rig.queue.hand(frame));
  }
  CHECK_EQ(display.blanks_waited(), 0u);
  CHECK(rig.thread.step());
  CHECK_EQ(display.blanks_waited(), 0u);
  CHECK_EQ(rig.surface.blits(), 1u);
  CHECK(rig.thread.step());
  CHECK_EQ(display.blanks_waited(), 1u);
  CHECK_EQ(rig.surface.blits(), 2u);
  display.kmt.advance(av::PacedDisplay::period + av::PacedDisplay::period / 2u);
  Frame late = rig.frame(0u, 3u);
  late.paced_by(&display.pacing);
  CHECK(rig.queue.hand(late));
  CHECK(rig.thread.step());
  CHECK_EQ(display.blanks_waited(), 1u);
  CHECK_EQ(rig.surface.blits(), 3u);
  CHECK_EQ(rig.queue.dropped_frames(), 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_frame_nothing_paces_is_shown_without_waiting_for_a_blank)
{ Rig          rig;
  av::PacedDisplay display;
  CHECK(display.pacing.ask(0));
  CHECK(rig.queue.start());
  for (std::uint32_t mark = 1u; mark <= 3u; mark++) {
      Frame frame = rig.frame(0u, mark);
      frame.paced_by(&display.pacing);
      CHECK(rig.queue.hand(frame));
      CHECK(rig.thread.step());
  }
  CHECK_EQ(display.blanks_waited(), 0u);
  CHECK_EQ(rig.surface.blits(), 3u); }

TEST(window_present_queue, a_handed_frame_is_shown_by_the_thread_and_its_owner_told_it_was_drawn)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK(rig.queue.hand(rig.frame(2u, 9u)));
  CHECK_EQ(rig.queue.queued(), 1u);
  CHECK_EQ(rig.handed.raises(), 1u);
  CHECK(rig.queue.invariants_hold());
  CHECK(rig.thread.step());
  CHECK_EQ(rig.readiness.waited(), 9u);
  CHECK_EQ(rig.surface.blits(), 1u);
  CHECK_EQ(rig.owner.finished(), 1u);
  CHECK_EQ(rig.owner.last_slot(), 2u);
  CHECK(rig.owner.last_drawn());
  CHECK_EQ(rig.queue.shown_frames(), 1u);
  CHECK_EQ(rig.queue.queued(), 0u);
  CHECK_EQ(rig.shown.raises(), 1u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_frame_whose_batch_is_gone_is_dropped_and_its_owner_told_it_was_not_drawn)
{ Rig rig;
  CHECK(rig.queue.start());
  rig.readiness.answer(FrameWait::gone);
  CHECK(rig.queue.hand(rig.frame(1u)));
  CHECK(rig.thread.step());
  CHECK_EQ(rig.surface.blits(), 0u);
  CHECK_EQ(rig.owner.finished(), 1u);
  CHECK(!rig.owner.last_drawn());
  CHECK_EQ(rig.queue.shown_frames(), 0u);
  CHECK_EQ(rig.queue.dropped_frames(), 1u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, showing_now_puts_the_frame_on_its_window_without_the_thread_or_the_counts)
{ Rig rig;
  CHECK(rig.queue.show_now(rig.frame(0u)));
  CHECK_EQ(rig.surface.blits(), 1u);
  CHECK(rig.surface.last_blit_dc() == &rig.dc);
  CHECK_EQ(rig.thread.starts(), 0u);
  CHECK_EQ(rig.queue.queued(), 0u);
  CHECK_EQ(rig.queue.shown_frames(), 0u);
  CHECK_EQ(rig.owner.finished(), 0u);
  rig.surface.refuse_next_blit();
  CHECK(!rig.queue.show_now(rig.frame(0u)));
  CHECK(rig.queue.show_now(rig.frame(0u)));
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_frame_a_chain_holds_is_presented_by_its_slot_when_shown_now)
{ Rig rig;
  rig.chains.refuse_opens(false);
  std::uint64_t ids[2] = {}, sizes[2] = {};
  CHECK(rig.chains.open(&rig.window, 1u, 4u, 2u, av::icd::SlotLayout::rgba_bottom_up, 2u, ids, sizes));
  const Frame chained(rig.owner, 1u, nullptr, Frame::Rows::bgra_bottom_up, 4u, 2u, &rig.window, Mark{});
  CHECK(rig.queue.show_now(chained));
  CHECK_EQ(rig.chains.presents(), 1u);
  CHECK_EQ(rig.chains.last_slot(), 1u);
  CHECK_EQ(rig.surface.blits(), 0u); }

TEST(window_present_queue, waiting_for_a_shown_frame_waits_on_the_shown_signal_for_the_time_given)
{ Rig rig;
  rig.queue.await_shown(7u);
  CHECK_EQ(rig.shown.waits(), 1u);
  CHECK_EQ(rig.shown.last_wait(), 7u);
  CHECK_EQ(rig.handed.waits(), 0u);
  CHECK(!rig.queue.await_shown(0u));
  CHECK_EQ(rig.shown.waits(), 2u);
  CHECK_EQ(rig.shown.last_wait(), 0u);
  rig.queue.announce();
  CHECK_EQ(rig.shown.raises(), 1u);
  CHECK(rig.queue.await_shown(5u)); }

TEST(window_present_queue, its_invariants_walk_the_rows_in_order_and_catch_a_repeated_order_or_a_wrong_scratch)
{ Rig rig;
  WindowPresentQueueProbe::set_next_order(rig.queue, 4u);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 1u);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, false, 0u);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 2u);
  CHECK(rig.queue.invariants_hold());
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 2u);
  CHECK(!rig.queue.invariants_hold());
  WindowPresentQueueProbe::clear_rows(rig.queue);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 0u);
  CHECK(!rig.queue.invariants_hold());
  WindowPresentQueueProbe::clear_rows(rig.queue);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 4u);
  CHECK(!rig.queue.invariants_hold());
  WindowPresentQueueProbe::clear_rows(rig.queue);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 1u);
  WindowPresentQueueProbe::add_row(rig.queue, rig.owner, true, 2u);
  WindowPresentQueueProbe::corrupt_rows(rig.queue);
  CHECK(!rig.queue.invariants_hold());
  WindowPresentQueueProbe::clear_rows(rig.queue);
  WindowPresentQueueProbe::set_scratch_bytes(rig.queue, 16u);
  CHECK(!rig.queue.invariants_hold());
  WindowPresentQueueProbe::set_scratch_bytes(rig.queue, 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_red_first_frame_is_traded_to_blue_first_in_a_scratch_that_grows_and_is_reused)
{ Rig rig;
  std::uint8_t big[8 * 4 * 4] = {};
  for (std::uint32_t at = 0u; at < sizeof big; at += 4u) {
      big[at] = 0x11u;
      big[at + 1u] = 0x22u;
      big[at + 2u] = 0x33u;
      big[at + 3u] = 0x44u;
  }
  const Frame small(rig.owner, 0u, rig.pixels, Frame::Rows::rgba_top_down, 4u, 2u, &rig.window, Mark{});
  const Frame large(rig.owner, 0u, big, Frame::Rows::rgba_top_down, 8u, 4u, &rig.window, Mark{});
  CHECK(rig.queue.show_now(small));
  CHECK_EQ(rig.surface.blits(), 1u);
  const std::uint32_t grown = rig.platform.live_allocations();
  CHECK(rig.queue.show_now(large));
  CHECK_EQ(rig.surface.blits(), 2u);
  CHECK_EQ(rig.platform.live_allocations(), grown);
  CHECK(rig.surface.last_blit_top_down());
  CHECK_EQ(rig.surface.last_blit_bytes()[0], 0x33u);
  CHECK_EQ(rig.surface.last_blit_bytes()[1], 0x22u);
  CHECK_EQ(rig.surface.last_blit_bytes()[2], 0x11u);
  CHECK_EQ(rig.surface.last_blit_bytes()[3], 0x44u);
  const std::uint32_t held = rig.platform.live_allocations();
  CHECK(rig.queue.show_now(small));
  CHECK_EQ(rig.surface.blits(), 3u);
  CHECK_EQ(rig.platform.live_allocations(), held);
  CHECK(rig.queue.invariants_hold());
  rig.surface.refuse_next_blit();
  CHECK(!rig.queue.show_now(small));
  CHECK_EQ(rig.surface.blits(), 3u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_red_first_frame_is_not_shown_when_it_cannot_be_traded)
{ Rig rig;
  const Frame empty(rig.owner, 0u, rig.pixels, Frame::Rows::rgba_top_down, 0u, 2u, &rig.window, Mark{});
  CHECK(!rig.queue.show_now(empty));
  CHECK_EQ(rig.surface.blits(), 0u);
  CHECK(rig.queue.invariants_hold());
  const Frame vast(rig.owner, 0u, rig.pixels, Frame::Rows::rgba_top_down, 0xFFFFFFFFu, 0xFFFFFFFFu, &rig.window, Mark{});
  CHECK(!rig.queue.show_now(vast));
  CHECK_EQ(rig.surface.blits(), 0u);
  CHECK(rig.queue.invariants_hold());
  const Frame small(rig.owner, 0u, rig.pixels, Frame::Rows::rgba_top_down, 4u, 2u, &rig.window, Mark{});
  CHECK(rig.queue.show_now(small));
  CHECK(!rig.queue.show_now(empty));
  CHECK_EQ(rig.surface.blits(), 1u);
  CHECK(rig.queue.invariants_hold());

  Rig starved;
  starved.platform.fail_allocation_at(1u);
  const Frame tiny(starved.owner, 0u, starved.pixels, Frame::Rows::rgba_top_down, 4u, 2u, &starved.window, Mark{});
  CHECK(!starved.queue.show_now(tiny));
  CHECK_EQ(starved.surface.blits(), 0u);
  CHECK(starved.queue.invariants_hold());
  CHECK(starved.queue.show_now(tiny));
  CHECK_EQ(starved.surface.blits(), 1u); }

TEST(window_present_queue, a_kept_owner_is_flushed_by_the_thread_and_forgotten_once_it_holds_nothing)
{ Rig rig;
  Keeper keeper;
  CHECK(rig.queue.start());
  rig.queue.keep(keeper);
  rig.queue.keep(keeper);
  CHECK_EQ(rig.handed.raises(), 2u);
  CHECK(rig.thread.step());
  CHECK_EQ(keeper.flushed(), 1u);
  CHECK_EQ(rig.handed.waits(), 0u);
  CHECK_EQ(rig.shown.raises(), 1u);
  CHECK(rig.thread.step());
  CHECK_EQ(keeper.flushed(), 1u);
  CHECK_EQ(rig.handed.last_wait(), Queue::idle_milliseconds);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, an_owner_that_still_holds_a_frame_sets_how_long_the_thread_rests)
{ Rig rig;
  Keeper first;
  Keeper second;
  first.later(17u);
  second.later(5u);
  CHECK(rig.queue.start());
  rig.queue.keep(first);
  rig.queue.keep(second);
  CHECK(rig.thread.step());
  CHECK_EQ(rig.handed.last_wait(), 5u);
  second.later(0u);
  CHECK(rig.thread.step());
  CHECK(rig.thread.step());
  CHECK_EQ(first.flushed(), 3u);
  CHECK_EQ(second.flushed(), 2u);
  CHECK_EQ(rig.handed.last_wait(), 17u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_queued_frame_is_shown_before_any_kept_owner_is_flushed)
{ Rig rig;
  Keeper keeper;
  CHECK(rig.queue.start());
  CHECK(rig.queue.hand(rig.frame(1u)));
  rig.queue.keep(keeper);
  CHECK(rig.thread.step());
  CHECK_EQ(keeper.flushed(), 0u);
  CHECK_EQ(rig.surface.blits(), 1u);
  CHECK(rig.thread.step());
  CHECK_EQ(keeper.flushed(), 1u); }

TEST(window_present_queue, a_forgotten_owner_is_not_flushed)
{ Rig rig;
  Keeper keeper;
  CHECK(rig.queue.start());
  rig.queue.keep(keeper);
  rig.queue.forget(keeper);
  rig.queue.forget(keeper);
  CHECK(rig.thread.step());
  CHECK_EQ(keeper.flushed(), 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_slot_is_looked_for_once_without_waiting)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK(rig.queue.hand(rig.frame(0u)));
  CHECK_EQ(rig.queue.try_slot(rig.owner, 2u, 2u), 1u);
  CHECK(rig.queue.hand(rig.frame(1u)));
  CHECK_EQ(rig.queue.try_slot(rig.owner, 2u, 2u), 2u);
  CHECK_EQ(rig.shown.waits(), 0u); }

TEST(window_present_queue, a_slot_is_reclaimed_from_the_owners_queued_frames_without_waiting)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK_EQ(rig.queue.reclaim_slot(rig.owner, 3u, 2u), 0u);
  CHECK(rig.queue.hand(rig.frame(0u)));
  CHECK(rig.queue.hand(rig.frame(1u)));
  CHECK_EQ(rig.queue.dropped_frames(), 0u);
  CHECK_EQ(rig.queue.reclaim_slot(rig.owner, 3u, 2u), 0u);
  CHECK_EQ(rig.queue.dropped_frames(), 2u);
  CHECK_EQ(rig.queue.queued(), 0u);
  CHECK_EQ(rig.shown.waits(), 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_slot_stays_taken_until_the_chain_has_finished_copying_it)
{ Rig rig;
  rig.chains.refuse_opens(false);
  std::uint64_t ids[2] = {}, sizes[2] = {};
  CHECK(rig.chains.open(&rig.window, 1u, 4u, 2u, av::icd::SlotLayout::rgba_bottom_up, 2u, ids, sizes));
  CHECK(rig.queue.start());
  const Frame chained(rig.owner, 0u, nullptr, Frame::Rows::bgra_bottom_up, 4u, 2u, &rig.window, Mark{});
  rig.chains.hold_copies(true);
  CHECK(rig.queue.hand(chained));
  CHECK(rig.thread.step());
  CHECK_EQ(rig.chains.presents(), 1u);
  CHECK_EQ(rig.owner.finished(), 1u);
  CHECK_EQ(rig.queue.shown_frames(), 1u);
  CHECK_EQ(rig.queue.try_slot(rig.owner, 1u, 1u), 1u);
  CHECK_EQ(rig.queue.try_slot(rig.owner, 2u, 2u), 1u);
  CHECK(rig.queue.invariants_hold());
  rig.chains.hold_copies(false);
  CHECK_EQ(rig.queue.try_slot(rig.owner, 1u, 1u), 0u);
  CHECK_EQ(rig.queue.queued(), 0u);
  CHECK_EQ(rig.shown.waits(), 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_slot_still_being_copied_is_not_reclaimed_and_is_let_go_when_its_owner_settles)
{ Rig rig;
  rig.chains.refuse_opens(false);
  std::uint64_t ids[2] = {}, sizes[2] = {};
  CHECK(rig.chains.open(&rig.window, 1u, 4u, 2u, av::icd::SlotLayout::rgba_bottom_up, 2u, ids, sizes));
  CHECK(rig.queue.start());
  rig.chains.hold_copies(true);
  CHECK(rig.queue.hand(Frame(rig.owner, 0u, nullptr, Frame::Rows::bgra_bottom_up, 4u, 2u, &rig.window, Mark{})));
  CHECK(rig.thread.step());
  CHECK_EQ(rig.queue.reclaim_slot(rig.owner, 1u, 1u), 1u);
  CHECK_EQ(rig.queue.dropped_frames(), 1u);
  CHECK_EQ(rig.queue.queued(), 1u);
  rig.queue.settle(rig.owner);
  CHECK_EQ(rig.queue.queued(), 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, a_slot_nobody_can_give_up_is_a_dropped_frame)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK_EQ(rig.queue.reclaim_slot(rig.owner, 1u, 0u), 1u);
  CHECK_EQ(rig.queue.dropped_frames(), 1u);
  CHECK_EQ(rig.shown.waits(), 0u); }

TEST(window_present_queue, waiting_for_a_slot_never_drops_the_frame_while_the_thread_runs)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK(rig.queue.hand(rig.frame(0u)));
  CHECK_EQ(rig.queue.try_slot(rig.owner, 1u, 1u), 1u);
  struct Late {
      Rig          &rig;
      std::uint32_t waits;
  } late{rig, 0u};
  rig.shown.on_wait([](void *context) {
      Late &later = *static_cast<Late *>(context);
      if (++later.waits == Queue::slot_looks + 10u) static_cast<void>(later.rig.thread.step());
  }, &late);
  CHECK_EQ(rig.queue.wait_slot(rig.owner, 1u, 1u), 0u);
  CHECK_EQ(late.waits, Queue::slot_looks + 10u);
  CHECK_EQ(rig.queue.dropped_frames(), 0u);
  CHECK_EQ(rig.queue.shown_frames(), 1u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, waiting_for_a_slot_ends_when_the_owner_s_transport_is_lost_while_the_thread_runs)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK(rig.queue.hand(rig.frame(0u)));
  struct Lost {
      Owner        &owner;
      std::uint32_t waits;
  } lost{rig.owner, 0u};
  rig.shown.on_wait([](void *context) {
      Lost &later = *static_cast<Lost *>(context);
      if (++later.waits == 3u) later.owner.lose();
  }, &lost);
  CHECK_EQ(rig.queue.wait_slot(rig.owner, 1u, 1u), 1u);
  CHECK_EQ(lost.waits, 3u);
  CHECK_EQ(rig.queue.dropped_frames(), 1u);
  CHECK_EQ(rig.queue.shown_frames(), 0u);
  CHECK(rig.queue.invariants_hold()); }

TEST(window_present_queue, waiting_for_a_slot_of_a_refused_queue_is_a_dropped_frame)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK(rig.queue.hand(rig.frame(0u)));
  rig.queue.refuse();
  CHECK_EQ(rig.queue.wait_slot(rig.owner, 1u, 1u), 1u);
  CHECK_EQ(rig.queue.dropped_frames(), 1u); }

namespace {

void a_frame_is_shown(void *context) { static_cast<void>(static_cast<Rig *>(context)->thread.step()); }

void run_paced(std::uint32_t interval, std::uint32_t slots, bool renders_into_slots)
{ constexpr std::uint32_t frames = 24u;
  constexpr std::uint32_t warm = 4u;
  Rig              rig;
  av::PacedDisplay display;
  CHECK(display.pacing.ask(static_cast<std::int32_t>(interval)));
  rig.chains.refuse_opens(false);
  rig.chains.settle_after_looks(1u);
  std::uint64_t ids[3] = {}, sizes[3] = {};
  CHECK(rig.chains.open(&rig.window, 1u, 4u, 2u, av::icd::SlotLayout::rgba_bottom_up, slots, ids, sizes));
  CHECK(rig.queue.start());
  rig.shown.on_wait(a_frame_is_shown, &rig);
  std::uint32_t drawn_into = 0u;
  std::uint32_t last = 0u;
  for (std::uint32_t frame = 0u; frame < frames; frame++) {
      const std::uint32_t slot = rig.queue.wait_slot(rig.owner, slots, renders_into_slots ? drawn_into : slots);
      CHECK(slot != slots);
      const std::uint32_t blanks = display.blanks_waited();
      if (frame >= warm) CHECK_EQ(blanks - last, interval);
      last = blanks;
      Frame handed(rig.owner, renders_into_slots ? drawn_into : slot, nullptr, Frame::Rows::bgra_bottom_up, 4u, 2u,
                   &rig.window, Mark{frame + 1u});
      handed.paced_by(&display.pacing);
      CHECK(rig.queue.hand(handed));
      drawn_into = slot;
  }
  static_cast<void>(rig.thread.run_until_done());
  CHECK_EQ(rig.queue.dropped_frames(), 0u);
  CHECK_EQ(rig.queue.shown_frames(), std::uint64_t{frames});
  CHECK_EQ(rig.chains.presents(), frames);
  CHECK(rig.queue.invariants_hold()); }

}

TEST(window_present_queue, paced_frames_are_let_in_one_per_frame_shown_however_late_a_slot_s_copy_settles)
{ run_paced(1u, 3u, true);
  run_paced(2u, 3u, true);
  run_paced(1u, 2u, false);
  run_paced(2u, 2u, false); }

TEST(window_present_queue, a_paced_frame_is_not_let_in_while_an_earlier_one_of_its_owner_is_not_shown)
{ Rig rig;
  CHECK(rig.queue.start());
  CHECK_EQ(rig.queue.paced_slot(rig.owner, 2u, 2u), 0u);
  CHECK(rig.queue.hand(rig.frame(0u)));
  CHECK_EQ(rig.queue.paced_slot(rig.owner, 2u, 2u), 2u);
  CHECK_EQ(rig.queue.try_slot(rig.owner, 2u, 2u), 1u);
  Owner other;
  CHECK_EQ(rig.queue.paced_slot(other, 2u, 2u), 0u);
  CHECK(rig.thread.step());
  CHECK_EQ(rig.queue.paced_slot(rig.owner, 2u, 2u), 0u);
  CHECK_EQ(rig.queue.dropped_frames(), 0u);
  CHECK_EQ(rig.shown.waits(), 0u); }
