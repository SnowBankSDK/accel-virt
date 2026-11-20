#ifndef AV_SESSION_RESERVATION_HPP
#define AV_SESSION_RESERVATION_HPP

#include <stdint.h>

namespace av {

class ByteLedger;

class SessionReservation {
public:
    SessionReservation();
    SessionReservation(const SessionReservation &) = delete;
    SessionReservation &operator=(const SessionReservation &) = delete;

    void               bind(ByteLedger *ledger);
    bool               binds(const ByteLedger *ledger) const;
    uint64_t           grant(uint64_t bytes, uint64_t least);
    [[nodiscard]] bool take(uint64_t bytes, bool session);
    void               give(uint64_t bytes);
    uint64_t           draw(uint64_t bytes);
    void               restore(uint64_t bytes);
    void               surrender(uint64_t bytes);
    uint64_t           close();
    uint64_t           idle() const;
    uint64_t           admissible() const;

private:
    uint64_t claim(uint64_t bytes);
    void     keep(uint64_t bytes);
    bool     reserve(uint64_t bytes);

    ByteLedger *ledger_;
    uint64_t    idle_;
    bool        open_;
};

}

#endif
