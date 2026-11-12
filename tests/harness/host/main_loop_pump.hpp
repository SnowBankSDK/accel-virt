#ifndef AV_MAIN_LOOP_PUMP_HPP
#define AV_MAIN_LOOP_PUMP_HPP

#include "harness/host/mock_host.hpp"

#include <cstdint>
#include <time.h>

namespace av {

inline std::uint64_t pump_clock_ms()
{
    timespec time = {};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return static_cast<std::uint64_t>(time.tv_sec) * 1000u + static_cast<std::uint64_t>(time.tv_nsec) / 1000000u;
}

template <typename Done>
bool pump_main_loop(MockHost &host, Done done, std::uint32_t patience_ms = 5000u)
{
    const std::uint64_t deadline = pump_clock_ms() + patience_ms;
    for (;;) {
        static_cast<void>(host.run_main_loop());
        if (done()) return true;
        if (pump_clock_ms() >= deadline) return false;
        static_cast<void>(host.wait_for_main_loop_work(1u, 10u));
    }
}

template <typename Device>
bool settle_device(MockHost &host, Device &device)
{
    return pump_main_loop(host, [&device] { return device.outstanding() == 0u; });
}

}

#endif
