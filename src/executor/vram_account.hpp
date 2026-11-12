#ifndef AV_VRAM_ACCOUNT_HPP
#define AV_VRAM_ACCOUNT_HPP

#include <stdint.h>

namespace av {

class ByteLedger;
class SessionLease;
class SessionReservation;

class VramAccount {
public:
    VramAccount();
    explicit VramAccount(ByteLedger &ledger);
    VramAccount(const VramAccount &) = delete;
    VramAccount &operator=(const VramAccount &) = delete;

    void               bind(ByteLedger *ledger);
    void               reserve_from(SessionLease *lease);
    bool               bound() const;
    bool               bound_to(const ByteLedger *ledger) const;
    [[nodiscard]] bool take(uint64_t bytes);
    [[nodiscard]] bool take_session(uint64_t bytes);
    void               give(uint64_t bytes);
    uint64_t           close();

    uint64_t held() const;
    uint64_t cap() const;
    uint64_t advertised() const;
    uint64_t free_bytes() const;
    uint64_t admissible() const;
    uint64_t used() const;

private:
    bool charge(uint64_t bytes, bool session);
    bool retake(uint64_t bytes, bool session);
    void refund(uint64_t bytes);

    ByteLedger         *ledger_;
    SessionReservation *reservation_;
    uint64_t            held_;
};

}

#endif
