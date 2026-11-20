#include "core/log/driver_log.hpp"
#include "guest/kmt/adapter_search.hpp"
#include "guest/kmt/avgpu_adapters.hpp"
#include "guest/suite/adapter_scenario.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/guest/mock_kmt.hpp"
#include "harness/icd/recording_log.hpp"
#include "icd/common/present_pacing.hpp"

#include <cstdint>

using av::MockKmt;
using av::icd::PresentPacing;

namespace av::icd {
struct FramePacerProbe {
    static void count_a_blank_without_an_adapter(FramePacer &pacer)
    {
        pacer.adapter_ = 0u;
        pacer.blanks_ = 1u;
    }
};

struct PresentPacingProbe {
    static FramePacer &pacer(PresentPacing &pacing) { return pacing.pacer_.object(); }
    static void        set_blanks(PresentPacing &pacing, std::uint32_t blanks) { pacing.blanks_ = blanks; }
};

}

using av::icd::FramePacerProbe;
using av::icd::PresentPacingProbe;

namespace {

class PacingRig {
public:
    PacingRig()
    { kmt_.add_adapter(av::PciIdentity(av::PciIdentity::virtio_vendor, av::AvgpuAdapters::virtio_gpu_device,
                                       av::PciIdentity::virtio_vendor, av::AvgpuAdapters::av_subsystem),
                       av::AdapterScenario::wddm_3_2);
      std::uint32_t adapters[4] = {};
      std::uint32_t found = 0u;
      CHECK_STATUS(kmt_.enumerate_adapters(adapters, 4u, found), av::Status::ok);
      adapter_ = adapters[0];
      CHECK_STATUS(kmt_.create_device(adapter_, device_), av::Status::ok); }

    av::Status bind(PresentPacing &pacing) { return pacing.bind(kmt_, lines_, adapter_, device_); }
    MockKmt   &kmt() { return kmt_; }

private:
    MockKmt           kmt_;
    av::RecordingLog  log_;
    av::DriverLog     lines_{log_, "avgl: "};
    std::uint32_t     adapter_ = 0u;
    std::uint32_t     device_ = 0u;
};

}

TEST(present_pacing, a_new_pacing_waits_for_one_blank)
{ PresentPacing pacing;
  CHECK(pacing.invariants_hold());
  CHECK_EQ(pacing.blanks(), 1u);
  CHECK_EQ(pacing.waited(), 0u); }

TEST(present_pacing, what_is_asked_for_within_range_is_kept)
{ PresentPacing pacing;
  for (std::int32_t asked = 0; asked <= static_cast<std::int32_t>(PresentPacing::max_blanks); asked++) {
      CHECK(pacing.ask(asked));
      CHECK_EQ(pacing.blanks(), static_cast<std::uint32_t>(asked));
      CHECK(pacing.invariants_hold());
  } }

TEST(present_pacing, an_interval_nobody_defines_is_refused_and_changes_nothing)
{ PresentPacing pacing;
  CHECK(pacing.ask(2));
  const std::int32_t refused[] = {-1, -100, static_cast<std::int32_t>(PresentPacing::max_blanks) + 1, 1000,
                                  -2147483647 - 1};
  for (const std::int32_t asked : refused) {
      CHECK(!pacing.ask(asked));
      CHECK_EQ(pacing.blanks(), 2u);
      CHECK(pacing.invariants_hold());
  } }

TEST(present_pacing, nothing_waits_or_drops_before_the_pacer_is_bound)
{ PresentPacing pacing;
  CHECK(pacing.admits());
  pacing.before_show();
  CHECK_EQ(pacing.waited(), 0u);
  CHECK(pacing.ask(0));
  CHECK(pacing.admits());
  CHECK(pacing.invariants_hold()); }

TEST(present_pacing, a_frame_after_one_shown_in_the_same_refresh_waits_for_the_next_blank_and_every_blank_waited_is_counted)
{ PacingRig rig;
  PresentPacing pacing;
  CHECK_STATUS(rig.bind(pacing), av::Status::ok);
  CHECK(pacing.invariants_hold());
  pacing.before_show();
  CHECK_EQ(pacing.waited(), 0u);
  pacing.before_show();
  CHECK_EQ(pacing.waited(), 1u);
  CHECK_EQ(rig.kmt().calls(MockKmt::Call::wait_for_vertical_blank), 1u);
  CHECK(pacing.ask(3));
  pacing.before_show();
  CHECK_EQ(pacing.waited(), 4u);
  CHECK_EQ(rig.kmt().calls(MockKmt::Call::wait_for_vertical_blank), 4u);
  CHECK(pacing.ask(0));
  pacing.before_show();
  CHECK_EQ(pacing.waited(), 4u);
  CHECK(pacing.invariants_hold()); }

namespace {

class BlankClock {
public:
    explicit BlankClock(PacingRig &rig) : rig_(rig)
    { rig_.kmt().set_blank_period(period);
      rig_.kmt().blank_on_boundaries();
      rig_.kmt().reads_clock(nullptr, 1u);
      rig_.kmt().advance(period / 3u); }

    static constexpr std::uint64_t period = MockKmt::ticks_per_second / 360u;

    void          work(std::uint64_t ticks) { rig_.kmt().advance(ticks); }
    std::uint64_t refresh_shown() const { return rig_.kmt().clock() / period + 1u; }

private:
    PacingRig &rig_;
};

std::uint64_t mean_refreshes_between_frames(PacingRig &rig, std::uint64_t work, std::uint32_t frames,
                                            std::uint32_t &shortest, std::uint32_t &longest)
{
    BlankClock    clock(rig);
    PresentPacing pacing;
    CHECK_STATUS(rig.bind(pacing), av::Status::ok);
    std::uint64_t first = 0u;
    std::uint64_t last = 0u;
    shortest = ~0u;
    longest = 0u;
    for (std::uint32_t frame = 0u; frame < frames; frame++) {
        clock.work(work);
        pacing.before_show();
        const std::uint64_t shown = clock.refresh_shown();
        if (frame == 0u) first = shown;
        if (frame != 0u) {
            const auto between = static_cast<std::uint32_t>(shown - last);
            if (between < shortest) shortest = between;
            if (between > longest) longest = between;
        }
        last = shown;
    }
    return (last - first) * 1000u / (frames - 1u);
}

}

TEST(present_pacing, frames_of_one_and_a_fifth_refreshes_of_work_are_shown_one_or_two_refreshes_apart_and_one_and_a_fifth_on_average)
{ PacingRig rig;
  std::uint32_t shortest = 0u, longest = 0u;
  const std::uint64_t mean = mean_refreshes_between_frames(rig, BlankClock::period * 6u / 5u, 201u, shortest, longest);
  CHECK_EQ(shortest, 1u);
  CHECK_EQ(longest, 2u);
  CHECK(mean >= 1190u && mean <= 1210u); }

TEST(present_pacing, frames_of_half_a_refresh_of_work_are_shown_one_every_refresh)
{ PacingRig rig;
  std::uint32_t shortest = 0u, longest = 0u;
  const std::uint64_t mean = mean_refreshes_between_frames(rig, BlankClock::period / 2u, 201u, shortest, longest);
  CHECK_EQ(shortest, 1u);
  CHECK_EQ(longest, 1u);
  CHECK_EQ(mean, 1000u); }

TEST(present_pacing, presenters_sharing_one_display_each_show_a_frame_every_refresh)
{ PacingRig     rig;
  BlankClock    clock(rig);
  constexpr std::uint32_t presenters = 4u;
  PresentPacing pacings[presenters];
  std::uint64_t last[presenters] = {};
  for (PresentPacing &pacing : pacings) CHECK_STATUS(rig.bind(pacing), av::Status::ok);
  for (std::uint32_t frame = 0u; frame < 100u; frame++) {
      clock.work(BlankClock::period * 9u / 10u);
      for (std::uint32_t presenter = 0u; presenter < presenters; presenter++) {
          pacings[presenter].before_show();
          const std::uint64_t shown = clock.refresh_shown();
          if (frame != 0u) CHECK_EQ(shown, last[presenter] + 1u);
          last[presenter] = shown;
      }
  } }

TEST(present_pacing, unbinding_stops_the_waiting)
{ PacingRig rig;
  PresentPacing pacing;
  CHECK_STATUS(rig.bind(pacing), av::Status::ok);
  pacing.unbind();
  pacing.before_show();
  pacing.before_show();
  CHECK_EQ(pacing.waited(), 0u);
  CHECK_EQ(rig.kmt().calls(MockKmt::Call::wait_for_vertical_blank), 0u);
  CHECK(pacing.invariants_hold()); }

TEST(present_pacing, its_invariants_catch_too_many_blanks_and_a_pacer_that_is_unsound)
{ PacingRig rig;
  PresentPacing pacing;
  CHECK_STATUS(rig.bind(pacing), av::Status::ok);
  CHECK(pacing.invariants_hold());
  PresentPacingProbe::set_blanks(pacing, PresentPacing::max_blanks + 1u);
  CHECK(!pacing.invariants_hold());
  PresentPacingProbe::set_blanks(pacing, 1u);
  CHECK(pacing.invariants_hold());
  FramePacerProbe::count_a_blank_without_an_adapter(PresentPacingProbe::pacer(pacing));
  CHECK(!pacing.invariants_hold()); }

TEST(present_pacing, the_next_admit_is_a_millisecond_away_until_a_pacer_knows_the_refresh)
{ PacingRig rig;
  PresentPacing pacing;
  CHECK_EQ(pacing.next_admit_milliseconds(), 1u);
  CHECK_STATUS(rig.bind(pacing), av::Status::ok);
  CHECK_EQ(pacing.next_admit_milliseconds(), 17u); }
