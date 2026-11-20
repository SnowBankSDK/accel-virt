#ifndef AV_HOST_LIFECYCLE_GATES_HPP
#define AV_HOST_LIFECYCLE_GATES_HPP

#include "executor/av_lifecycle_gates.h"
#include "executor/host_driver.hpp"
#include "executor/host_gpu_set.hpp"
#include "executor/host_lifecycle_gate.hpp"
#include "proto/context/device_uuid.hpp"

#include <stdint.h>

namespace av {

class HostLifecycleGates {
public:
    HostLifecycleGates();
    explicit HostLifecycleGates(EglEntryGate &egl);
    HostLifecycleGates(const HostLifecycleGates &) = delete;
    HostLifecycleGates &operator=(const HostLifecycleGates &) = delete;

    HostLifecycleGate      *add(const uint8_t *uuid, const HostDriver &driver);
    HostLifecycleGate      *of(const uint8_t *uuid);
    const AvLifecycleGates &abi() const;

private:
    AvLifecycleGates  abi_;
    HostLifecycleGate gates_[HostGpuSet::max_gpus];
    HostDriver        drivers_[HostGpuSet::max_gpus];
    uint32_t          drivers_count_;
    DeviceUuid        uuids_[HostGpuSet::max_gpus];
    uint32_t          gate_of_[HostGpuSet::max_gpus];
    uint32_t          count_;
};

}

#endif
