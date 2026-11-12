#ifndef AV_EXECUTOR_D3D_LEASED_OBJECTS_HPP
#define AV_EXECUTOR_D3D_LEASED_OBJECTS_HPP

#include "core/support/keyed_slots.hpp"

#include <stdint.h>

namespace av {

class BlockSource;
class SessionLease;

class LeasedObjects {
public:
    explicit LeasedObjects(BlockSource &blocks);
    LeasedObjects(const LeasedObjects &) = delete;
    LeasedObjects &operator=(const LeasedObjects &) = delete;

    void     draw_from(SessionLease *lease);
    void     charge(uint32_t id, uint64_t bytes);
    void     recharge(uint32_t id, uint64_t bytes);
    void     settle();
    void     release(uint32_t id);
    void     clear();
    uint64_t charged(uint32_t id) const;
    uint32_t count() const;

private:
    KeyedSlots<uint32_t, uint64_t> charges_;
    SessionLease                  *lease_;
    uint64_t                       drawn_;
    uint64_t                       owed_;
};

}

#endif
