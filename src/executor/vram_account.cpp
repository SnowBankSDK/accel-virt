#include "executor/vram_account.hpp"

#include "core/trace/function_scope.hpp"
#include "executor/byte_ledger.hpp"
#include "executor/ledger_relief.hpp"
#include "executor/session_lease.hpp"
#include "executor/session_reservation.hpp"

namespace av {

VramAccount::VramAccount() : ledger_(nullptr), reservation_(nullptr), held_(0u)
{
    AV_SCOPE("VramAccount::VramAccount");
}

VramAccount::VramAccount(ByteLedger &ledger) : ledger_(&ledger), reservation_(nullptr), held_(0u)
{
    AV_SCOPE("VramAccount::VramAccount");
}

void VramAccount::bind(ByteLedger *ledger)
{
    AV_SCOPE("VramAccount::bind");
    static_cast<void>(close());
    ledger_ = ledger;
    reservation_ = nullptr;
}

void VramAccount::reserve_from(SessionLease *lease)
{
    AV_SCOPE("VramAccount::reserve_from");
    reservation_ = lease != nullptr ? lease->reservation_for(ledger_) : nullptr;
}

bool VramAccount::bound() const
{
    AV_SCOPE("VramAccount::bound");
    return ledger_ != nullptr;
}

bool VramAccount::bound_to(const ByteLedger *ledger) const
{
    AV_SCOPE("VramAccount::bound_to");
    return ledger_ == ledger;
}

bool VramAccount::take(uint64_t bytes)
{
    AV_SCOPE("VramAccount::take");
    if (ledger_ != nullptr && !charge(bytes, false) && !retake(bytes, false)) return false;
    __atomic_fetch_add(&held_, bytes, __ATOMIC_RELAXED);
    return true;
}

bool VramAccount::take_session(uint64_t bytes)
{
    AV_SCOPE("VramAccount::take_session");
    if (ledger_ != nullptr && !charge(bytes, true) && !retake(bytes, true)) return false;
    __atomic_fetch_add(&held_, bytes, __ATOMIC_RELAXED);
    return true;
}

bool VramAccount::retake(uint64_t bytes, bool session)
{
    AV_SCOPE("VramAccount::retake");
    LedgerRelief *const relief = ledger_->relief();
    if (relief == nullptr) return false;
    relief->relieve(*ledger_, this);
    return charge(bytes, session);
}

bool VramAccount::charge(uint64_t bytes, bool session)
{
    AV_SCOPE("VramAccount::charge");
    if (reservation_ != nullptr) return reservation_->take(bytes, session);
    return session ? ledger_->take_session(bytes) : ledger_->take(bytes);
}

void VramAccount::refund(uint64_t bytes)
{
    AV_SCOPE("VramAccount::refund");
    if (ledger_ == nullptr) return;
    if (reservation_ != nullptr) reservation_->give(bytes);
    else ledger_->give(bytes);
}

void VramAccount::give(uint64_t bytes)
{
    AV_SCOPE("VramAccount::give");
    uint64_t held = __atomic_load_n(&held_, __ATOMIC_RELAXED);
    uint64_t returned = 0u;
    do {
        returned = bytes < held ? bytes : held;
    } while (!__atomic_compare_exchange_n(&held_, &held, held - returned, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
    refund(returned);
}

uint64_t VramAccount::close()
{
    AV_SCOPE("VramAccount::close");
    const uint64_t left = __atomic_exchange_n(&held_, 0u, __ATOMIC_ACQ_REL);
    refund(left);
    return left;
}

uint64_t VramAccount::held() const
{
    AV_SCOPE("VramAccount::held");
    return __atomic_load_n(&held_, __ATOMIC_ACQUIRE);
}

uint64_t VramAccount::advertised() const
{
    AV_SCOPE("VramAccount::advertised");
    return ledger_ != nullptr ? ledger_->advertised() : UINT64_MAX;
}

uint64_t VramAccount::cap() const
{
    AV_SCOPE("VramAccount::cap");
    return ledger_ != nullptr ? ledger_->cap() : UINT64_MAX;
}

uint64_t VramAccount::free_bytes() const
{
    AV_SCOPE("VramAccount::free_bytes");
    return ledger_ != nullptr ? ledger_->free_bytes() : UINT64_MAX;
}

uint64_t VramAccount::admissible() const
{
    AV_SCOPE("VramAccount::admissible");
    if (reservation_ != nullptr) return reservation_->admissible();
    return ledger_ != nullptr ? ledger_->admissible() : UINT64_MAX;
}

uint64_t VramAccount::used() const
{
    AV_SCOPE("VramAccount::used");
    return ledger_ != nullptr ? ledger_->used() : 0u;
}

}
