#include "executor/session_reservation.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/byte_ledger.hpp"
#include "executor/ledger_relief.hpp"

namespace av {

SessionReservation::SessionReservation() : ledger_(nullptr), idle_(0u), open_(false)
{
    AV_SCOPE("SessionReservation::SessionReservation");
}

void SessionReservation::bind(ByteLedger *ledger)
{
    AV_SCOPE("SessionReservation::bind");
    static_cast<void>(close());
    ledger_ = ledger;
    __atomic_store_n(&open_, ledger != nullptr, __ATOMIC_RELEASE);
}

bool SessionReservation::binds(const ByteLedger *ledger) const
{
    AV_SCOPE("SessionReservation::binds");
    return ledger != nullptr && ledger_ == ledger;
}

uint64_t SessionReservation::grant(uint64_t bytes, uint64_t least)
{
    AV_SCOPE("SessionReservation::grant");
    if (bytes == 0u || ledger_ == nullptr) return bytes;
    if (!__atomic_load_n(&open_, __ATOMIC_ACQUIRE)) return 0u;
    if (reserve(bytes)) return bytes;
    LedgerRelief *const relief = ledger_->relief();
    if (relief != nullptr) relief->relieve(*ledger_, nullptr);
    if (reserve(bytes)) return bytes;
    return least != 0u && least < bytes && reserve(least) ? least : 0u;
}

bool SessionReservation::reserve(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::reserve");
    if (!ledger_->take(bytes)) return false;
    keep(bytes);
    return true;
}

bool SessionReservation::take(uint64_t bytes, bool session)
{
    AV_SCOPE("SessionReservation::take");
    if (ledger_ == nullptr) return true;
    const uint64_t claimed = claim(bytes);
    const uint64_t rest = bytes - claimed;
    if (rest == 0u || (session ? ledger_->take_session(rest) : ledger_->take(rest))) return true;
    keep(claimed);
    return false;
}

void SessionReservation::give(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::give");
    if (ledger_ == nullptr || bytes == 0u) return;
    if (__atomic_load_n(&open_, __ATOMIC_ACQUIRE)) keep(bytes);
    else ledger_->give(bytes);
}

void SessionReservation::draw(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::draw");
    if (ledger_ == nullptr) return;
    const uint64_t claimed = claim(bytes);
    if (claimed != 0u) ledger_->give(claimed);
}

void SessionReservation::restore(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::restore");
    if (ledger_ == nullptr || bytes == 0u || !__atomic_load_n(&open_, __ATOMIC_ACQUIRE)) return;
    ledger_->hold(bytes);
    keep(bytes);
}

void SessionReservation::surrender(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::surrender");
    draw(bytes);
}

uint64_t SessionReservation::close()
{
    AV_SCOPE("SessionReservation::close");
    __atomic_store_n(&open_, false, __ATOMIC_RELEASE);
    if (ledger_ == nullptr) return 0u;
    const uint64_t claimed = claim(UINT64_MAX);
    if (claimed != 0u) ledger_->give(claimed);
    return claimed;
}

uint64_t SessionReservation::idle() const
{
    AV_SCOPE("SessionReservation::idle");
    return __atomic_load_n(&idle_, __ATOMIC_ACQUIRE);
}

uint64_t SessionReservation::admissible() const
{
    AV_SCOPE("SessionReservation::admissible");
    if (ledger_ == nullptr) return UINT64_MAX;
    const uint64_t room = ledger_->admissible();
    const uint64_t held = idle();
    return room > UINT64_MAX - held ? UINT64_MAX : room + held;
}

uint64_t SessionReservation::claim(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::claim");
    uint64_t idle = __atomic_load_n(&idle_, __ATOMIC_ACQUIRE);
    uint64_t claimed = 0u;
    do {
        claimed = bytes < idle ? bytes : idle;
    } while (claimed != 0u &&
             !__atomic_compare_exchange_n(&idle_, &idle, idle - claimed, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    return claimed;
}

void SessionReservation::keep(uint64_t bytes)
{
    AV_SCOPE("SessionReservation::keep");
    __atomic_fetch_add(&idle_, bytes, __ATOMIC_ACQ_REL);
}

}
