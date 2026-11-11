#ifndef AV_VRAM_LEDGER_HPP
#define AV_VRAM_LEDGER_HPP

#include "executor/byte_ledger.hpp"
#include "proto/context/vram_policy.hpp"

#include <stdint.h>

namespace av {

class HostRamLedger;
class VramBudgetSource;

class VramLedger final : public ByteLedger {
public:
    VramLedger();

    [[nodiscard]] bool take(uint64_t bytes) override;
    [[nodiscard]] bool take_session(uint64_t bytes) override;
    void               give(uint64_t bytes) override;

    void learn(uint64_t total);
    void share_host_ram(HostRamLedger *host);
    void follow(VramBudgetSource *source);
    void reserve_vram(uint32_t mib);

    uint64_t total() const;
    uint64_t advertised() const override;
    uint64_t admissible() const override;
    bool     shares_host_ram() const;

    bool invariants_hold() const;

private:
    void     recompute();
    void     refresh();
    bool     charge_host(uint64_t bytes, bool session);
    uint64_t policy_cap() const;

    uint64_t   total_;
    VramPolicy policy_;
    VramBudgetSource *source_;
    HostRamLedger    *host_;
    uint64_t   stamp_;
};

}

#endif
