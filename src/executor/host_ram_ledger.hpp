#ifndef AV_HOST_RAM_LEDGER_HPP
#define AV_HOST_RAM_LEDGER_HPP

#include "executor/byte_ledger.hpp"

#include <stdint.h>

namespace av {

class HostMemoryGauge;

class HostRamLedger final : public ByteLedger {
public:
    static constexpr uint64_t refresh_ms = 100u;
    static constexpr uint64_t relieve_ms = 1000u;

    HostRamLedger();

    [[nodiscard]] bool take(uint64_t bytes) override;
    [[nodiscard]] bool take_session(uint64_t bytes) override;
    void               give(uint64_t bytes) override;
    uint64_t           admissible() const override;

    void     budget(uint64_t bytes);
    uint64_t budget() const;
    void     watch(HostMemoryGauge *gauge);
    void     floor(uint64_t bytes);
    uint64_t floor() const;
    uint64_t available() const;
    uint64_t credit() const;
    uint64_t tight_refusals() const;

private:
    static constexpr uint64_t never = UINT64_MAX;

    bool admit(uint64_t bytes);
    void refresh();
    void relieve();
    void restore(uint64_t bytes);
    void spend(uint64_t bytes);

    uint64_t         budget_;
    HostMemoryGauge *gauge_;
    uint64_t         floor_;
    uint64_t         stamp_;
    uint64_t         available_;
    uint64_t         seen_;
    uint64_t         credit_;
    uint64_t         relieved_;
    uint64_t         tight_refusals_;
};

}

#endif
