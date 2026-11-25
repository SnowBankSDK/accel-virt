#ifndef AV_FENCE_WAIT_HPP
#define AV_FENCE_WAIT_HPP

#include "device/shim/posix_lock.hpp"
#include "executor/monotonic_clock.hpp"
#include "executor/owned_fd.hpp"

#include <stdint.h>

struct ID3D11Fence;

namespace av {

class FenceWait {
public:
    FenceWait();
    FenceWait(const FenceWait &) = delete;
    FenceWait &operator=(const FenceWait &) = delete;

    [[nodiscard]] bool wait(ID3D11Fence *fence, uint64_t value, uint32_t wait_ms);

private:
    void drain();

    OwnedFd        event_;
    PosixLock      waiting_;
    MonotonicClock clock_;
};

}

#endif
