#include "executor/session_lease.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

SessionLease::SessionLease() : reservations_{}
{
    AV_SCOPE("SessionLease::SessionLease");
}

void SessionLease::bind(ByteLedger *device, ByteLedger *host)
{
    AV_SCOPE("SessionLease::bind");
    reservations_[static_cast<uint32_t>(LeaseRecord::Kind::device)].bind(device);
    reservations_[static_cast<uint32_t>(LeaseRecord::Kind::host)].bind(host);
}

void SessionLease::open(const uint64_t *initial)
{
    AV_SCOPE("SessionLease::open");
    for (uint32_t kind = 0u; kind < LeaseRecord::kinds; kind++)
        static_cast<void>(reservations_[kind].grant(initial[kind], 0u));
}

uint64_t SessionLease::extend(uint32_t kind, uint64_t bytes, uint64_t least)
{
    AV_SCOPE("SessionLease::extend");
    if (kind >= LeaseRecord::kinds) return 0u;
    return reservations_[kind].grant(bytes, least);
}

void SessionLease::surrender(uint32_t kind, uint64_t bytes)
{
    AV_SCOPE("SessionLease::surrender");
    if (kind < LeaseRecord::kinds) reservations_[kind].surrender(bytes);
}

SessionReservation &SessionLease::of(LeaseRecord::Kind kind)
{
    AV_SCOPE("SessionLease::of");
    return reservations_[static_cast<uint32_t>(kind)];
}

SessionReservation *SessionLease::reservation_for(const ByteLedger *ledger)
{
    AV_SCOPE("SessionLease::reservation_for");
    for (SessionReservation &reservation : reservations_)
        if (reservation.binds(ledger)) return &reservation;
    return nullptr;
}

uint64_t SessionLease::close()
{
    AV_SCOPE("SessionLease::close");
    uint64_t returned = 0u;
    for (SessionReservation &reservation : reservations_) returned += reservation.close();
    return returned;
}

uint64_t SessionLease::idle(LeaseRecord::Kind kind) const
{
    AV_SCOPE("SessionLease::idle");
    return reservations_[static_cast<uint32_t>(kind)].idle();
}

}
