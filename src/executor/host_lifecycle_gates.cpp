#include "executor/host_lifecycle_gates.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/egl_entry_gate.hpp"

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

int gates_admit_device(void *gate, VkPhysicalDevice_T *physical, const VkDeviceCreateInfo *create, uint32_t *slots)
{
    AV_SCOPE("gates_admit_device");
    return static_cast<HostLifecycleGate *>(gate)->devices().admit(physical, *create, *slots) ? 1 : 0;
}

void gates_return_device(void *gate, uint32_t slots)
{
    AV_SCOPE("gates_return_device");
    static_cast<HostLifecycleGate *>(gate)->devices().give(slots);
}

}

HostLifecycleGates::HostLifecycleGates() : HostLifecycleGates(EglEntryGate::process())
{
    AV_SCOPE("HostLifecycleGates::HostLifecycleGates");
}

HostLifecycleGates::HostLifecycleGates(EglEntryGate &egl)
    : abi_{}, gates_(), drivers_(), drivers_count_(0u), uuids_(), gate_of_{}, count_(0u)
{
    AV_SCOPE("HostLifecycleGates::HostLifecycleGates");
    abi_.size = sizeof(AvLifecycleGates);
    abi_.self = this;
    abi_.gate_of = gates_gate_of;
    abi_.enter = gates_enter;
    abi_.leave = gates_leave;
    abi_.share = gates_share;
    abi_.unshare = gates_unshare;
    abi_.admit_device = gates_admit_device;
    abi_.return_device = gates_return_device;
    for (HostLifecycleGate &gate : gates_) gate.guard_egl(egl);
}

HostLifecycleGate *HostLifecycleGates::add(const uint8_t *uuid, const HostDriver &driver)
{
    AV_SCOPE("HostLifecycleGates::add");
    HostLifecycleGate *const known = of(uuid);
    if (known != nullptr) return known;
    const uint32_t at = __atomic_load_n(&count_, __ATOMIC_ACQUIRE);
    if (uuid == nullptr || at == HostGpuSet::max_gpus) return nullptr;
    uint32_t gate = 0u;
    while (gate < drivers_count_ && !drivers_[gate].same_library(driver)) gate++;
    if (gate == drivers_count_) {
        drivers_[gate] = driver;
        gates_[gate].driven_by(driver.id());
        drivers_count_++;
    }
    uuids_[at] = DeviceUuid(uuid);
    gate_of_[at] = gate;
    __atomic_store_n(&count_, at + 1u, __ATOMIC_RELEASE);
    return &gates_[gate];
}

HostLifecycleGate *HostLifecycleGates::of(const uint8_t *uuid)
{
    AV_SCOPE("HostLifecycleGates::of");
    if (uuid == nullptr) return nullptr;
    const uint32_t count = __atomic_load_n(&count_, __ATOMIC_ACQUIRE);
    for (uint32_t at = 0u; at < count; at++)
        if (uuids_[at].equals(uuid)) return &gates_[gate_of_[at]];
    return nullptr;
}

const AvLifecycleGates &HostLifecycleGates::abi() const
{
    AV_SCOPE("HostLifecycleGates::abi");
    return abi_;
}

}
