#include "executor/vulkan/dedicated_imports.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

DedicatedImports::Payload::Payload(uint64_t memory, uint64_t bytes) : memory_(memory), bytes_(bytes), fd_()
{
    AV_SCOPE("DedicatedImports::Payload::Payload");
}

DedicatedImports::DedicatedImports(BlockSource &blocks) : payloads_(blocks), dedicated_(blocks, Pool::paged)
{
    AV_SCOPE("DedicatedImports::DedicatedImports");
}

Status DedicatedImports::keep_payload(uint64_t memory, int fd, uint64_t bytes)
{
    AV_SCOPE("DedicatedImports::keep_payload");
    if (memory == 0u || fd < 0 || bytes == 0u) return Status::invalid_parameter;
    drop_payload(memory);
    Payload *made = nullptr;
    if (payloads_.create(made, memory, bytes) != Status::ok) return Status::no_memory;
    if (made->fd_.hold_copy(fd)) return Status::ok;
    static_cast<void>(payloads_.destroy(made));
    return Status::no_memory;
}

const DedicatedImports::Payload *DedicatedImports::find(uint64_t memory) const
{
    AV_SCOPE("DedicatedImports::find");
    const Payload *found = nullptr;
    payloads_.for_each([memory, &found](const Payload &held) {
        if (held.memory_ == memory) found = &held;
    });
    return found;
}

int DedicatedImports::payload(uint64_t memory) const
{
    AV_SCOPE("DedicatedImports::payload");
    const Payload *const held = find(memory);
    return held != nullptr ? held->fd_.fd() : -1;
}

uint64_t DedicatedImports::payload_bytes(uint64_t memory) const
{
    AV_SCOPE("DedicatedImports::payload_bytes");
    const Payload *const held = find(memory);
    return held != nullptr ? held->bytes_ : 0u;
}

void DedicatedImports::drop_payload(uint64_t memory)
{
    AV_SCOPE("DedicatedImports::drop_payload");
    Payload *const held = payloads_.find_if([memory](const Payload &candidate) { return candidate.memory_ == memory; });
    if (held != nullptr) static_cast<void>(payloads_.destroy(held));
}

Status DedicatedImports::dedicate(uint64_t image, uint64_t memory)
{
    AV_SCOPE("DedicatedImports::dedicate");
    if (image == 0u || memory == 0u || dedicated_.find(image) != nullptr) return Status::invalid_parameter;
    uint32_t slot = 0u;
    return dedicated_.insert(image, memory, slot);
}

uint64_t DedicatedImports::take_dedicated(uint64_t image)
{
    AV_SCOPE("DedicatedImports::take_dedicated");
    uint64_t memory = 0u;
    return dedicated_.take(image, memory) ? memory : 0u;
}

uint32_t DedicatedImports::payloads() const
{
    AV_SCOPE("DedicatedImports::payloads");
    return payloads_.live();
}

uint32_t DedicatedImports::dedicated() const
{
    AV_SCOPE("DedicatedImports::dedicated");
    return dedicated_.count();
}

bool DedicatedImports::invariants_hold() const
{
    AV_SCOPE("DedicatedImports::invariants_hold");
    bool held = true;
    payloads_.for_each([&held](const Payload &payload) {
        held = held && payload.memory_ != 0u && payload.bytes_ != 0u && payload.fd_.held();
    });
    dedicated_.for_each([&held](uint64_t image, const uint64_t &memory) { held = held && image != 0u && memory != 0u; });
    return held && dedicated_.invariants_hold();
}

}
