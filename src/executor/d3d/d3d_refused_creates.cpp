#include "executor/d3d/d3d_refused_creates.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

D3dRefusedCreates::D3dRefusedCreates() : rows_(), count_(0u)
{
    AV_SCOPE("D3dRefusedCreates::D3dRefusedCreates");
}

bool D3dRefusedCreates::note(uint32_t id, uint32_t hresult)
{
    AV_SCOPE("D3dRefusedCreates::note");
    if (find(id) != most || count_ == most) return false;
    rows_[count_].id_ = id;
    rows_[count_].hresult_ = hresult;
    count_++;
    return true;
}

bool D3dRefusedCreates::forget(uint32_t id)
{
    AV_SCOPE("D3dRefusedCreates::forget");
    const uint32_t at = find(id);
    if (at == most) return false;
    count_--;
    rows_[at] = rows_[count_];
    return true;
}

bool D3dRefusedCreates::holds(uint32_t id) const
{
    AV_SCOPE("D3dRefusedCreates::holds");
    return find(id) != most;
}

uint32_t D3dRefusedCreates::hresult_of(uint32_t id) const
{
    AV_SCOPE("D3dRefusedCreates::hresult_of");
    const uint32_t at = find(id);
    return at == most ? 0u : rows_[at].hresult_;
}

uint32_t D3dRefusedCreates::count() const
{
    AV_SCOPE("D3dRefusedCreates::count");
    return count_;
}

void D3dRefusedCreates::clear()
{
    AV_SCOPE("D3dRefusedCreates::clear");
    count_ = 0u;
}

uint32_t D3dRefusedCreates::find(uint32_t id) const
{
    for (uint32_t at = 0u; at < count_; at++)
        if (rows_[at].id_ == id) return at;
    return most;
}

bool D3dRefusedCreates::invariants_hold() const
{
    AV_SCOPE("D3dRefusedCreates::invariants_hold");
    if (count_ > most) return false;
    for (uint32_t at = 0u; at < count_; at++)
        for (uint32_t other = at + 1u; other < count_; other++)
            if (rows_[at].id_ == rows_[other].id_) return false;
    return true;
}

}
