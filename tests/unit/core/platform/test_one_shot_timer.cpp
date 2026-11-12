#include "core/platform/one_shot_timer.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/mock_platform.hpp"
#include "harness/platform/violation.hpp"

#include <cstdint>

using av::Irql;
using av::MockPlatform;
using av::OneShotTimer;
using av::Violation;

namespace {

void count(void *calls) { ++*static_cast<std::uint32_t *>(calls); }

}

TEST(one_shot_timer_arm, runs_its_routine_once_at_dispatch_when_the_time_passes)
{ MockPlatform platform; std::uint32_t calls = 0u;
  OneShotTimer timer(platform, &count, &calls);
  platform.set_irql(Irql::dispatch);
  timer.arm(100u);
  platform.set_irql(Irql::passive);
  CHECK_EQ(platform.armed_timers(), 1u);
  platform.advance_time(99u);
  CHECK_EQ(platform.fire_due_timers(), 0u);
  platform.advance_time(1u);
  CHECK_EQ(platform.fire_due_timers(), 1u);
  CHECK_EQ(calls, 1u);
  platform.advance_time(1000u);
  CHECK_EQ(platform.fire_due_timers(), 0u);
  CHECK_EQ(calls, 1u);
  CHECK_EQ(platform.unclaimed_violations(), 0u); }

TEST(one_shot_timer_arm, arming_again_replaces_the_due_time)
{ MockPlatform platform; std::uint32_t calls = 0u;
  OneShotTimer timer(platform, &count, &calls);
  timer.arm(100u);
  timer.arm(300u);
  CHECK_EQ(platform.armed_timers(), 1u);
  CHECK_EQ(platform.timer_due(0u), 300u);
  platform.advance_time(200u);
  CHECK_EQ(platform.fire_due_timers(), 0u);
  CHECK_EQ(calls, 0u); }

TEST(one_shot_timer_cancel, a_cancelled_or_quiesced_timer_never_runs_and_can_be_armed_again)
{ MockPlatform platform; std::uint32_t calls = 0u;
  OneShotTimer timer(platform, &count, &calls);
  timer.arm(10u);
  timer.cancel();
  CHECK_EQ(platform.armed_timers(), 0u);
  timer.arm(10u);
  timer.quiesce();
  platform.advance_time(50u);
  CHECK_EQ(platform.fire_due_timers(), 0u);
  CHECK_EQ(calls, 0u);
  timer.arm(10u);
  platform.advance_time(10u);
  CHECK_EQ(platform.fire_due_timers(), 1u);
  CHECK_EQ(calls, 1u); }

TEST(one_shot_timer_cancel, a_timer_destroyed_while_armed_never_runs)
{ MockPlatform platform; std::uint32_t calls = 0u;
  {
      OneShotTimer timer(platform, &count, &calls);
      timer.arm(10u);
  }
  CHECK_EQ(platform.armed_timers(), 0u);
  platform.advance_time(50u);
  CHECK_EQ(platform.fire_due_timers(), 0u);
  CHECK_EQ(calls, 0u); }

TEST(one_shot_timer_arm, above_dispatch_is_a_violation)
{ MockPlatform platform; std::uint32_t calls = 0u;
  OneShotTimer timer(platform, &count, &calls);
  platform.set_irql(Irql::isr);
  timer.arm(10u);
  platform.set_irql(Irql::passive);
  CHECK(platform.claim(Violation::irql_ceiling));
  CHECK(platform.claim(Violation::irql_ceiling));
  CHECK_EQ(platform.unclaimed_violations(), 0u); }
