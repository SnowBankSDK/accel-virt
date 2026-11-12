#include "device/context/device_context_table.hpp"

#include "core/support/text_buffer.hpp"
#include "core/trace/function_scope.hpp"
#include "core/trace/state_writer.hpp"
#include "device/context/context_binding.hpp"
#include "device/abi/entry_points.hpp"
#include "device/host/host.hpp"
#include "proto/context/sub_api.hpp"

namespace av {

static_assert(DevicePulse::capacity >= DeviceContextTable::max_contexts, "the pulse carries every context's page");

DeviceContextTable::DeviceContextTable(Host &host, BlobTable &blobs, HostBlobOwners &owners,
                                       FenceInterrupts &interrupts, const ExecutorLibrary &library,
                                       WireRecorder &recorder)
    : host_(host), blobs_(blobs), owners_(owners), interrupts_(interrupts), library_(library),
      recorder_(recorder), shares_(host, library), ordering_(host),
      pulse_(host), slots_{}, retired_{}, reaper_(host, &AvContextsReaped, this)
    {
        AV_SCOPE("DeviceContextTable::DeviceContextTable");
    }

DeviceContextTable::~DeviceContextTable()
{
    AV_SCOPE("DeviceContextTable::~DeviceContextTable");
    if (retire_all() != 0u) reaper_.abandon();
    else reaper_.stop();
}

Status DeviceContextTable::initialize()
{
    AV_SCOPE("DeviceContextTable::initialize");
    host_.require_main_loop("DeviceContextTable::initialize");
    const Status ordered = ordering_.initialize();
    if (ordered != Status::ok) return ordered;
    const Status pulsing = pulse_.initialize();
    return pulsing != Status::ok ? pulsing : shares_.initialize();
}

void DeviceContextTable::settle_d3d()
{
    AV_SCOPE("DeviceContextTable::settle_d3d");
    ordering_.settle();
}

bool DeviceContextTable::can_share(uint32_t asking, uint32_t share)
{
    AV_SCOPE("DeviceContextTable::can_share");
    host_.require_main_loop("DeviceContextTable::can_share");
    const DeviceContext *const shared = asking != share ? find(share) : nullptr;
    return shared != nullptr && shared->binding().bound() && shared->binding().sub_api() == SubApi::opengl;
}

ObjectStorage<DeviceContext> *DeviceContextTable::slot_of(uint32_t id)
{
    AV_SCOPE("DeviceContextTable::slot_of");
    for (uint32_t index = 0u; index < max_contexts; index++)
        if (live_at(index) && slots_[index].object().id() == id) return &slots_[index];
    return nullptr;
}

bool DeviceContextTable::live_at(uint32_t index) const
{
    AV_SCOPE("DeviceContextTable::live_at");
    return slots_[index].constructed() && !retired_[index];
}

void DeviceContextTable::retire(uint32_t index)
{
    AV_SCOPE("DeviceContextTable::retire");
    DeviceContext &context = slots_[index].object();
    context.retire();
    if (!context.worker().running()) {
        slots_[index].destroy();
        return;
    }
    if (!reaper_.running()) static_cast<void>(reaper_.start());
    if (reaper_.running() && reaper_.take(context.worker(), index) == Status::ok) {
        retired_[index] = true;
        return;
    }
    slots_[index].destroy();
}

void DeviceContextTable::finish_reaped()
{
    AV_SCOPE("DeviceContextTable::finish_reaped");
    host_.require_main_loop("DeviceContextTable::finish_reaped");
    uint32_t       reaped[max_contexts] = {};
    const uint32_t count = reaper_.collect(reaped, max_contexts);
    for (uint32_t at = 0u; at < count; at++) {
        slots_[reaped[at]].destroy();
        retired_[reaped[at]] = false;
    }
}

uint32_t DeviceContextTable::reaping() const
{
    AV_SCOPE("DeviceContextTable::reaping");
    uint32_t count = 0u;
    for (bool retired : retired_) count += retired ? 1u : 0u;
    return count;
}

Status DeviceContextTable::create(uint32_t id, const char *name)
{
    AV_SCOPE("DeviceContextTable::create");
    host_.require_main_loop("DeviceContextTable::create");
    if (id == 0u) return Status::invalid_parameter;
    if (slot_of(id) != nullptr) return Status::conflict;
    finish_reaped();
    for (ObjectStorage<DeviceContext> &slot : slots_) {
        if (slot.construct(host_, blobs_, owners_, interrupts_, library_, shares_, recorder_, ordering_, pulse_,
                           id, name) != Status::ok)
            continue;
        const Status status = slot.object().start();
        if (status != Status::ok) slot.destroy();
        return status;
    }
    return Status::no_space;
}

Status DeviceContextTable::destroy(uint32_t id)
{
    AV_SCOPE("DeviceContextTable::destroy");
    host_.require_main_loop("DeviceContextTable::destroy");
    ObjectStorage<DeviceContext> *const slot = slot_of(id);
    if (slot == nullptr) return Status::not_found;
    retire(static_cast<uint32_t>(slot - slots_));
    return Status::ok;
}

void DeviceContextTable::destroy_all()
{
    AV_SCOPE("DeviceContextTable::destroy_all");
    host_.require_main_loop("DeviceContextTable::destroy_all");
    if (retire_all() != 0u) reaper_.resume();
    else reaper_.stop();
}

uint32_t DeviceContextTable::retire_all()
{
    AV_SCOPE("DeviceContextTable::retire_all");
    uint32_t retired = 0u;
    for (uint32_t index = 0u; index < max_contexts; index++) {
        if (!live_at(index)) continue;
        retire(index);
        retired++;
    }
    const bool     waits = retired != 0u || reaper_.fresh() != 0u;
    const uint64_t now = host_.monotonic_nanoseconds();
    uint32_t       overdue = 0u;
    const uint32_t stuck = reaper_.drain(waits ? now + teardown_budget_nanoseconds : now, overdue);
    finish_reaped();
    for (uint32_t index = 0u; index < max_contexts; index++)
        if (!retired_[index]) slots_[index].destroy();
    if (overdue == 0u) return stuck;
    char       bytes[192];
    TextBuffer message(bytes, sizeof bytes);
    message.append("virtio-gpu-av: ");
    message.append_decimal(overdue);
    message.append(" context worker(s) did not exit within ");
    message.append_decimal(teardown_budget_nanoseconds / 1000000u);
    message.append(" ms of the teardown; they are left running and keep their slots and pins until they exit");
    host_.log(message.text());
    return stuck;
}

DeviceContext *DeviceContextTable::find(uint32_t id)
{
    AV_SCOPE("DeviceContextTable::find");
    host_.require_main_loop("DeviceContextTable::find");
    ObjectStorage<DeviceContext> *const slot = slot_of(id);
    return slot != nullptr ? &slot->object() : nullptr;
}

bool DeviceContextTable::retiring_pins(uint32_t resource)
{
    AV_SCOPE("DeviceContextTable::retiring_pins");
    host_.require_main_loop("DeviceContextTable::retiring_pins");
    for (uint32_t at = 0u; at < max_contexts; at++)
        if (retired_[at] && slots_[at].object().binding().pins(resource)) return true;
    return false;
}

bool DeviceContextTable::holds_doorbell(uint32_t index)
{
    AV_SCOPE("DeviceContextTable::holds_doorbell");
    host_.require_main_loop("DeviceContextTable::holds_doorbell");
    for (uint32_t at = 0u; at < max_contexts; at++)
        if (live_at(at) && slots_[at].object().binding().bound() && slots_[at].object().binding().doorbell() == index)
            return true;
    return false;
}

uint32_t DeviceContextTable::live() const
{
    AV_SCOPE("DeviceContextTable::live");
    uint32_t count = 0u;
    for (uint32_t index = 0u; index < max_contexts; index++) count += live_at(index) ? 1u : 0u;
    return count;
}

bool DeviceContextTable::invariants_hold() const
{
    AV_SCOPE("DeviceContextTable::invariants_hold");
    for (uint32_t first = 0u; first < max_contexts; first++) {
        if (retired_[first] && !slots_[first].constructed()) return false;
        if (!live_at(first)) continue;
        const DeviceContext &context = slots_[first].object();
        if (!context.running() || context.id() == 0u) return false;
        for (uint32_t second = first + 1u; second < max_contexts; second++)
            if (live_at(second) && slots_[second].object().id() == context.id()) return false;
    }
    return reaper_.invariants_hold();
}

void DeviceContextTable::report(StateWriter &writer)
{
    AV_SCOPE("DeviceContextTable::report");
    writer.open("contexts", 0u);
    writer.number("live", live());
    writer.number("reaping", reaping());
    for (uint32_t index = 0u; index < max_contexts; index++)
        if (live_at(index)) slots_[index].object().report(writer);
    writer.close();
}

}
