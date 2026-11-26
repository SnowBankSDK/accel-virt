#ifndef AV_BINDABLE_MEMORY_HPP
#define AV_BINDABLE_MEMORY_HPP

#include "core/support/keyed_slots.hpp"
#include "core/support/status.hpp"

#include <stdint.h>

namespace av {

class BlockSource;

class BindableMemory {
public:
    explicit BindableMemory(BlockSource &blocks);
    BindableMemory(const BindableMemory &) = delete;
    BindableMemory &operator=(const BindableMemory &) = delete;

    [[nodiscard]] Status note(uint64_t memory, uint64_t bytes);
    void                 forget(uint64_t memory);
    bool                 fits(uint64_t memory, uint64_t offset, uint64_t bytes) const;
    uint32_t             count() const;

    bool invariants_hold() const;

private:
    KeyedSlots<uint64_t, uint64_t> sizes_;
};

}

#endif
