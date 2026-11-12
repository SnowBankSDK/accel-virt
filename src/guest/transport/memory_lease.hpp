#ifndef AV_MEMORY_LEASE_HPP
#define AV_MEMORY_LEASE_HPP

#include "proto/submission/lease_record.hpp"

#include <stdint.h>

namespace av {

class ClientTransport;

class MemoryLease {
public:
    static constexpr uint64_t first_chunk = uint64_t{16} << 20u;
    static constexpr uint64_t largest_chunk = uint64_t{256} << 20u;
    static constexpr uint64_t kept_chunks = 2u;

    MemoryLease();
    MemoryLease(const MemoryLease &) = delete;
    MemoryLease &operator=(const MemoryLease &) = delete;

    [[nodiscard]] bool take(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes);
    void               give(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes);
    void               reset();
    uint64_t           left(LeaseRecord::Kind kind) const;
    uint64_t           extensions() const;

    bool invariants_hold() const;

private:
    struct Pool {
        uint64_t left_;
        uint64_t chunk_;
    };

    bool     draw(Pool &pool, uint64_t bytes);
    uint64_t extend(ClientTransport &transport, LeaseRecord::Kind kind, uint64_t bytes, uint64_t least);
    void     settle(ClientTransport &transport, LeaseRecord::Kind kind, Pool &pool);
    Pool    &pool(LeaseRecord::Kind kind);

    Pool     pools_[LeaseRecord::kinds];
    uint64_t extensions_;
};

}

#endif
