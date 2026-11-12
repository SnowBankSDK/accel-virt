#ifndef AV_HOST_LIFECYCLE_GATE_HPP
#define AV_HOST_LIFECYCLE_GATE_HPP

#include <pthread.h>
#include <stdint.h>

namespace av {

class HostLifecycleGate {
public:
    static constexpr uint64_t default_wait_ns = 10000000000ull;

    explicit HostLifecycleGate(uint64_t wait_ns = default_wait_ns);
    ~HostLifecycleGate();
    HostLifecycleGate(const HostLifecycleGate &) = delete;
    HostLifecycleGate &operator=(const HostLifecycleGate &) = delete;

    [[nodiscard]] bool enter();
    void               leave();
    uint64_t           entries() const;
    uint64_t           refusals() const;

private:
    pthread_mutex_t lock_;
    uint64_t        wait_ns_;
    uint64_t        entries_;
    uint64_t        refusals_;
};

class HostLifecycleHold {
public:
    explicit HostLifecycleHold(HostLifecycleGate *gate);
    ~HostLifecycleHold();
    HostLifecycleHold(const HostLifecycleHold &) = delete;
    HostLifecycleHold &operator=(const HostLifecycleHold &) = delete;

    bool held() const;

private:
    HostLifecycleGate *gate_;
    bool               held_;
};

}

#endif
