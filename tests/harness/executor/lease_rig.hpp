#ifndef AV_LEASE_RIG_HPP
#define AV_LEASE_RIG_HPP

#include "executor/session_lease.hpp"
#include "executor/vram_account.hpp"
#include "guest/transport/memory_lease.hpp"
#include "harness/transport/lease_host_transport.hpp"

#include <cstdint>

namespace av {

class ByteLedger;

class LeaseRig {
public:
    LeaseRig(ByteLedger *vram, ByteLedger *host, std::uint64_t room,
             const std::uint64_t *initial = LeaseRecord::initial_bytes);
    ~LeaseRig();
    LeaseRig(const LeaseRig &) = delete;
    LeaseRig &operator=(const LeaseRig &) = delete;

    SessionLease &host();
    MemoryLease  &guest();
    bool          admit(std::uint64_t bytes);
    void          credit(std::uint64_t bytes);
    std::uint64_t in_use() const;
    std::uint64_t room() const;

private:
    ByteLedger        *vram_;
    SessionLease       lease_;
    LeaseHostTransport transport_;
    MemoryLease        guest_;
    VramAccount        filler_;
};

}

#endif
