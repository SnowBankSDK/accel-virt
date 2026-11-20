#ifndef AV_ICD_LEASED_HANDLES_HPP
#define AV_ICD_LEASED_HANDLES_HPP

#include "core/support/keyed_slots.hpp"
#include "guest/transport/memory_lease.hpp"
#include "proto/submission/lease_record.hpp"

#include <stdint.h>

namespace av {
class BlockSource;
class ClientTransport;
}

namespace av::icd {

class LeasedHandles {
public:
    explicit LeasedHandles(BlockSource &memory);
    LeasedHandles(const LeasedHandles &) = delete;
    LeasedHandles &operator=(const LeasedHandles &) = delete;

    [[nodiscard]] bool take(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes);
    void               keep(uint64_t handle, LeaseRecord::Kind kind, uint64_t bytes);
    void               refund(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes);
    void               release(ClientTransport &transport, uint64_t handle);
    void               reset();
    uint64_t           left(LeaseRecord::Kind kind) const;
    uint32_t           count() const;

private:
    struct Held {
        uint64_t bytes_;
        uint32_t kind_;
    };

    MemoryLease            lease_;
    KeyedSlots<uint64_t, Held> held_;
};

}

#endif
