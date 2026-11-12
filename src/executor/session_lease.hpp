#ifndef AV_SESSION_LEASE_HPP
#define AV_SESSION_LEASE_HPP

#include "executor/session_reservation.hpp"
#include "proto/submission/lease_record.hpp"

#include <stdint.h>

namespace av {

class ByteLedger;

class SessionLease {
public:
    SessionLease();
    SessionLease(const SessionLease &) = delete;
    SessionLease &operator=(const SessionLease &) = delete;

    void                bind(ByteLedger *device, ByteLedger *host);
    void                open(const uint64_t *initial = LeaseRecord::initial_bytes);
    uint64_t            extend(uint32_t kind, uint64_t bytes, uint64_t least);
    void                surrender(uint32_t kind, uint64_t bytes);
    SessionReservation &of(LeaseRecord::Kind kind);
    SessionReservation *reservation_for(const ByteLedger *ledger);
    uint64_t            close();
    uint64_t            idle(LeaseRecord::Kind kind) const;

private:
    SessionReservation reservations_[LeaseRecord::kinds];
};

}

#endif
