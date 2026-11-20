#ifndef AV_LEDGER_RELIEF_HPP
#define AV_LEDGER_RELIEF_HPP

namespace av {

class ByteLedger;
class VramAccount;

class LedgerRelief {
public:
    virtual void relieve(const ByteLedger &ledger, const VramAccount *taker) = 0;

protected:
    ~LedgerRelief() = default;
};

}

#endif
