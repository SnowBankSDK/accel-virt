#ifndef AV_EXPORT_TURN_HPP
#define AV_EXPORT_TURN_HPP

#include <stdint.h>

namespace av {

class ExportTurn {
public:
    static constexpr uint32_t images = 2u;

    ExportTurn();

    void     begin();
    void     place(uint32_t image, uint32_t slot);
    uint32_t slot(uint32_t image) const;
    uint32_t next() const;
    void     handed(uint32_t slot);
    uint32_t shown_as(uint32_t slot) const;

    bool invariants_hold() const;

private:
    uint32_t slots_[images];
    uint32_t turn_;

    friend struct ExportTurnProbe;
};

}

#endif
