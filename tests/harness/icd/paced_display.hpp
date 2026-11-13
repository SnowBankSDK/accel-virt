#ifndef AV_PACED_DISPLAY_HPP
#define AV_PACED_DISPLAY_HPP

#include "core/log/driver_log.hpp"
#include "guest/kmt/avgpu_adapters.hpp"
#include "guest/suite/adapter_scenario.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/guest/mock_kmt.hpp"
#include "harness/icd/recording_log.hpp"
#include "icd/common/present_pacing.hpp"

#include <cstdint>

namespace av {

class PacedDisplay {
public:
    PacedDisplay()
    { kmt.add_adapter(PciIdentity(PciIdentity::virtio_vendor, AvgpuAdapters::virtio_gpu_device,
                                  PciIdentity::virtio_vendor, AvgpuAdapters::av_subsystem),
                      AdapterScenario::wddm_3_2);
      std::uint32_t adapters[4] = {};
      std::uint32_t found = 0u;
      CHECK_STATUS(kmt.enumerate_adapters(adapters, 4u, found), Status::ok);
      std::uint32_t device = 0u;
      CHECK_STATUS(kmt.create_device(adapters[0], device), Status::ok);
      kmt.set_blank_period(period);
      kmt.blank_on_boundaries();
      kmt.reads_clock(nullptr, 1u);
      CHECK_STATUS(pacing.bind(kmt, lines, adapters[0], device), Status::ok); }

    static constexpr std::uint64_t period = MockKmt::ticks_per_second / 60u;

    std::uint32_t blanks_waited() const { return kmt.calls(MockKmt::Call::wait_for_vertical_blank); }

    MockKmt            kmt;
    RecordingLog       log;
    DriverLog          lines{log, "avtest: "};
    icd::PresentPacing pacing;
};

}

#endif
