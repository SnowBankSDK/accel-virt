#include "executor/host_lifecycle_gates.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

namespace {

void *gates_gate_of(void *self, const uint8_t *device_uuid)
{
    AV_SCOPE("gates_gate_of");
    return static_cast<HostLifecycleGates *>(self)->of(device_uuid);
}

int gates_enter(void *gate)
{
    AV_SCOPE("gates_enter");
    return static_cast<HostLifecycleGate *>(gate)->enter() ? 1 : 0;
}

void gates_leave(void *gate)
{
    AV_SCOPE("gates_leave");
    static_cast<HostLifecycleGate *>(gate)->leave();
}

int gates_share(void *gate)
{
    AV_SCOPE("gates_share");
    return static_cast<HostLifecycleGate *>(gate)->share() ? 1 : 0;
}

void gates_unshare(void *gate)
{
    AV_SCOPE("gates_unshare");
    static_cast<HostLifecycleGate *>(gate)->unshare();
}

}

HostLifecycleGates::HostLifecycleGates() : abi_{}, gates_(), uuids_(), count_(0u)
{
    AV_SCOPE("HostLifecycleGates::HostLifecycleGates");
    abi_.size = sizeof(AvLifecycleGates);
    abi_.self = this;
    abi_.gate_of = gates_gate_of;
    abi_.enter = gates_enter;
    abi_.leave = gates_leave;
    abi_.share = gates_share;
    abi_.unshare = gates_unshare;
}

HostLifecycleGate *HostLifecycleGates::add(const uint8_t *uuid, VkDriverId driver)
{
    AV_SCOPE("HostLifecycleGates::add");
    HostLifecycleGate *const known = of(uuid);
    if (known != nullptr) return known;
    const uint32_t at = __atomic_load_n(&count_, __ATOMIC_ACQUIRE);
    if (uuid == nullptr || at == HostGpuSet::max_gpus) return nullptr;
    uuids_[at] = DeviceUuid(uuid);
    gates_[at].driven_by(driver);
    __atomic_store_n(&count_, at + 1u, __ATOMIC_RELEASE);
    return &gates_[at];
}

HostLifecycleGate *HostLifecycleGates::of(const uint8_t *uuid)
{
    AV_SCOPE("HostLifecycleGates::of");
    if (uuid == nullptr) return nullptr;
    const uint32_t count = __atomic_load_n(&count_, __ATOMIC_ACQUIRE);
    for (uint32_t at = 0u; at < count; at++)
        if (uuids_[at].equals(uuid)) return &gates_[at];
    return nullptr;
}

const AvLifecycleGates &HostLifecycleGates::abi() const
{
    AV_SCOPE("HostLifecycleGates::abi");
    return abi_;
}

}
