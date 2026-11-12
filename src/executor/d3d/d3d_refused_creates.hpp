#ifndef AV_EXECUTOR_D3D_REFUSED_CREATES_HPP
#define AV_EXECUTOR_D3D_REFUSED_CREATES_HPP

#include "core/support/growable_array.hpp"

#include <stdint.h>

namespace av {

class BlockSource;

class D3dRefusedCreates {
public:
    explicit D3dRefusedCreates(BlockSource &blocks);
    D3dRefusedCreates(const D3dRefusedCreates &) = delete;
    D3dRefusedCreates &operator=(const D3dRefusedCreates &) = delete;

    [[nodiscard]] bool note(uint32_t id, uint32_t hresult);
    bool               forget(uint32_t id);
    bool               holds(uint32_t id) const;
    uint32_t           hresult_of(uint32_t id) const;
    uint32_t           count() const;
    void               clear();

    bool invariants_hold() const;

private:
    struct Refusal {
        uint32_t id_;
        uint32_t hresult_;
    };

    static constexpr uint32_t absent = UINT32_MAX;

    uint32_t find(uint32_t id) const;

    GrowableArray<Refusal> rows_;
};

}

#endif
