#include "harness/framework/test_registry.hpp"
#include "harness/guest/mock_kmt.hpp"
#include "harness/icd/lock_trace.hpp"
#include "harness/icd/recording_thread_lock.hpp"
#include "harness/icd/scripted_gpu_timeline.hpp"
#include "icd/common/joint_timeline.hpp"
#include "icd/common/lane_progress.hpp"

#include <cstdint>

using av::icd::JointTimeline;

namespace {

class FakeLane final : public av::icd::LaneProgress {
public:
    std::uint64_t covering() const override { return covering_; }
    bool          ran(std::uint64_t fence) const override { return ran_ >= fence; }

    std::uint64_t covering_ = 0u;
    std::uint64_t ran_ = 0u;
};

class Fixture {
public:
    Fixture() : lock_(trace_, 'j'), joint_(session_, lock_, kmt_) {}

    av::ScriptedGpuTimeline &session() { return session_; }
    JointTimeline           &joint() { return joint_; }
    bool                     unlocked() const { return !lock_.held(); }

private:
    av::MockKmt             kmt_;
    av::LockTrace           trace_;
    av::RecordingThreadLock lock_;
    av::ScriptedGpuTimeline session_;
    JointTimeline           joint_;
};

}

TEST(joint_timeline, without_lanes_a_mark_completes_when_the_session_reaches_it)
{ Fixture fixture;
  fixture.session().send();
  fixture.session().send();
  const std::uint64_t mark = fixture.joint().covering();
  CHECK(mark != 0u);
  CHECK(fixture.joint().completed() < mark);
  fixture.session().finish(1u);
  CHECK(fixture.joint().completed() < mark);
  fixture.session().finish(2u);
  CHECK(fixture.joint().completed() >= mark);
  CHECK_EQ(fixture.joint().covering(), fixture.joint().covering());
  CHECK(fixture.unlocked()); }

TEST(joint_timeline, memory_retired_while_a_lane_has_work_waits_for_that_lane_as_well_as_the_session)
{ Fixture fixture;
  FakeLane lane;
  fixture.joint().join(lane);
  fixture.session().send();
  lane.covering_ = 4u;
  const std::uint64_t mark = fixture.joint().recording();
  fixture.session().finish(5u);
  CHECK(fixture.joint().completed() < mark);
  lane.ran_ = 3u;
  CHECK(fixture.joint().completed() < mark);
  lane.ran_ = 4u;
  CHECK(fixture.joint().completed() >= mark);
  fixture.joint().leave(lane); }

TEST(joint_timeline, marks_stay_in_order_and_one_lane_done_does_not_complete_a_mark_another_lane_still_owes)
{ Fixture fixture;
  FakeLane first;
  FakeLane second;
  fixture.joint().join(first);
  fixture.joint().join(second);
  first.covering_ = 2u;
  const std::uint64_t early = fixture.joint().covering();
  second.covering_ = 7u;
  const std::uint64_t late = fixture.joint().covering();
  CHECK(late > early);
  fixture.session().finish(10u);
  second.ran_ = 7u;
  CHECK(fixture.joint().completed() < early);
  first.ran_ = 2u;
  CHECK(fixture.joint().completed() >= late);
  fixture.joint().leave(second);
  fixture.joint().leave(first); }

TEST(joint_timeline, a_lane_that_left_after_its_work_ran_owes_nothing_and_a_new_lane_in_its_place_owes_only_its_own)
{ Fixture fixture;
  FakeLane gone;
  fixture.joint().join(gone);
  gone.covering_ = 9u;
  const std::uint64_t mark = fixture.joint().covering();
  fixture.joint().leave(gone);
  FakeLane fresh;
  fixture.joint().join(fresh);
  fresh.covering_ = 1u;
  const std::uint64_t later = fixture.joint().covering();
  fixture.session().finish(1u);
  CHECK(fixture.joint().completed() >= mark);
  CHECK(fixture.joint().completed() < later);
  fresh.ran_ = 1u;
  CHECK(fixture.joint().completed() >= later);
  fixture.joint().leave(fresh); }

TEST(joint_timeline, a_wait_drives_the_session_and_then_waits_for_the_lanes)
{ Fixture fixture;
  FakeLane lane;
  fixture.joint().join(lane);
  fixture.session().send();
  lane.covering_ = 3u;
  lane.ran_ = 3u;
  const std::uint64_t mark = fixture.joint().covering();
  CHECK(fixture.joint().wait(mark));
  CHECK(fixture.session().waits() != 0u);
  CHECK(fixture.joint().completed() >= mark);
  CHECK(!fixture.joint().wait(mark + 5u));
  fixture.joint().leave(lane); }

TEST(joint_timeline, past_its_marks_a_new_mark_folds_into_the_last_and_never_completes_early)
{ Fixture fixture;
  FakeLane lane;
  fixture.joint().join(lane);
  std::uint64_t last = 0u;
  for (std::uint64_t index = 1u; index <= JointTimeline::marks + 8u; index++) {
      lane.covering_ = index;
      last = fixture.joint().covering();
  }
  fixture.session().finish(1u);
  lane.ran_ = JointTimeline::marks;
  CHECK(fixture.joint().completed() < last);
  lane.ran_ = JointTimeline::marks + 8u;
  CHECK(fixture.joint().completed() >= last);
  fixture.joint().leave(lane); }
