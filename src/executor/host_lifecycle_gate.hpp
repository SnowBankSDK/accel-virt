#ifndef AV_HOST_LIFECYCLE_GATE_HPP
#define AV_HOST_LIFECYCLE_GATE_HPP

#include <pthread.h>
#include <stdint.h>

#include <vulkan/vulkan_core.h>

namespace av {

class HostLifecycleGate {
public:
    static constexpr uint64_t default_wait_ns = 10000000000ull;

    explicit HostLifecycleGate(uint64_t wait_ns = default_wait_ns);
    ~HostLifecycleGate();
    HostLifecycleGate(const HostLifecycleGate &) = delete;
    HostLifecycleGate &operator=(const HostLifecycleGate &) = delete;

    void               driven_by(VkDriverId driver);
    bool               shares_compiles() const;
    [[nodiscard]] bool enter();
    void               leave();
    [[nodiscard]] bool share();
    void               unshare();
    uint64_t           entries() const;
    uint64_t           shares() const;
    uint64_t           refusals() const;

private:
    timespec deadline() const;

    pthread_rwlock_t lock_;
    uint64_t         wait_ns_;
    uint64_t         entries_;
    uint64_t         shares_;
    uint64_t         refusals_;
    bool             shares_compiles_;
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

class HostLifecycleShare {
public:
    explicit HostLifecycleShare(HostLifecycleGate *gate);
    ~HostLifecycleShare();
    HostLifecycleShare(const HostLifecycleShare &) = delete;
    HostLifecycleShare &operator=(const HostLifecycleShare &) = delete;

    bool held() const;

private:
    HostLifecycleGate *gate_;
    bool               held_;
};

}

#endif
