#include "executor/vulkan/bindable_memory.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

BindableMemory::BindableMemory(BlockSource &blocks) : sizes_(blocks, Pool::paged)
{
    AV_SCOPE("BindableMemory::BindableMemory");
}

Status BindableMemory::note(uint64_t memory, uint64_t bytes)
{
    AV_SCOPE("BindableMemory::note");
    if (memory == 0u || bytes == 0u) return Status::invalid_parameter;
    uint64_t *const known = sizes_.find(memory);
    if (known != nullptr) {
        *known = bytes;
        return Status::ok;
    }
    uint32_t slot = 0u;
    return sizes_.insert(memory, bytes, slot);
}

void BindableMemory::forget(uint64_t memory)
{
    AV_SCOPE("BindableMemory::forget");
    static_cast<void>(sizes_.erase(memory));
}

bool BindableMemory::fits(uint64_t memory, uint64_t offset, uint64_t bytes) const
{
    AV_SCOPE("BindableMemory::fits");
    const uint64_t *const known = sizes_.find(memory);
    return known != nullptr && offset <= *known && bytes <= *known - offset;
}

uint32_t BindableMemory::count() const
{
    AV_SCOPE("BindableMemory::count");
    return sizes_.count();
}

bool BindableMemory::invariants_hold() const
{
    AV_SCOPE("BindableMemory::invariants_hold");
    bool named = true;
    sizes_.for_each([&named](uint64_t memory, const uint64_t &bytes) { named = named && memory != 0u && bytes != 0u; });
    return named && sizes_.invariants_hold();
}

}
