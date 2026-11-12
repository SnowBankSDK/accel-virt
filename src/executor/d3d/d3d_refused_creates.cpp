#include "executor/d3d/d3d_refused_creates.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

D3dRefusedCreates::D3dRefusedCreates(BlockSource &blocks) : rows_(blocks, Pool::paged)
{
    AV_SCOPE("D3dRefusedCreates::D3dRefusedCreates");
}

bool D3dRefusedCreates::note(uint32_t id, uint32_t hresult)
{
    AV_SCOPE("D3dRefusedCreates::note");
    if (find(id) != absent) return false;
    return rows_.append(Refusal{id, hresult}) == Status::ok;
}

bool D3dRefusedCreates::forget(uint32_t id)
{
    AV_SCOPE("D3dRefusedCreates::forget");
    const uint32_t at = find(id);
    if (at == absent) return false;
    rows_.remove_swap(at);
    return true;
}

bool D3dRefusedCreates::holds(uint32_t id) const
{
    AV_SCOPE("D3dRefusedCreates::holds");
    return find(id) != absent;
}

uint32_t D3dRefusedCreates::hresult_of(uint32_t id) const
{
    AV_SCOPE("D3dRefusedCreates::hresult_of");
    const uint32_t at = find(id);
    return at == absent ? 0u : rows_.at(at).hresult_;
}

uint32_t D3dRefusedCreates::count() const
{
    AV_SCOPE("D3dRefusedCreates::count");
    return rows_.size();
}

void D3dRefusedCreates::clear()
{
    AV_SCOPE("D3dRefusedCreates::clear");
    rows_.clear();
}

uint32_t D3dRefusedCreates::find(uint32_t id) const
{
    AV_SCOPE("D3dRefusedCreates::find");
    for (uint32_t at = 0u; at < rows_.size(); at++)
        if (rows_.at(at).id_ == id) return at;
    return absent;
}

bool D3dRefusedCreates::invariants_hold() const
{
    AV_SCOPE("D3dRefusedCreates::invariants_hold");
    if (!rows_.invariants_hold()) return false;
    for (uint32_t at = 0u; at < rows_.size(); at++)
        for (uint32_t other = at + 1u; other < rows_.size(); other++)
            if (rows_.at(at).id_ == rows_.at(other).id_) return false;
    return true;
}

}
