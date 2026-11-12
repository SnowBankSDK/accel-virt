#ifndef AV_ONE_SHOT_TIMER_HPP
#define AV_ONE_SHOT_TIMER_HPP

#include "core/platform/platform.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class OneShotTimer {
public:
    static constexpr size_t storage_bytes = 192u;

    OneShotTimer(Platform &platform, TimerRoutine routine, void *context);
    ~OneShotTimer();
    OneShotTimer(const OneShotTimer &) = delete;
    OneShotTimer &operator=(const OneShotTimer &) = delete;

    void arm(uint64_t ticks);
    void cancel();
    void quiesce();

private:
    Platform    &platform_;
    TimerRoutine routine_;
    void        *context_;
    alignas(16) unsigned char storage_[storage_bytes];
};

}

#endif
