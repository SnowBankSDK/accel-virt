#include "executor/d3d/export_turn.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

ExportTurn::ExportTurn() : slots_{0u, 1u}, turn_(0u)
{
    AV_SCOPE("ExportTurn::ExportTurn");
}

void ExportTurn::begin()
{
    AV_SCOPE("ExportTurn::begin");
    turn_ = 0u;
}

void ExportTurn::place(uint32_t image, uint32_t slot)
{
    AV_SCOPE("ExportTurn::place");
    if (image < images) slots_[image] = slot;
}

uint32_t ExportTurn::slot(uint32_t image) const
{
    AV_SCOPE("ExportTurn::slot");
    return slots_[image < images ? image : 0u];
}

uint32_t ExportTurn::next() const
{
    AV_SCOPE("ExportTurn::next");
    return slots_[turn_];
}

void ExportTurn::handed(uint32_t slot)
{
    AV_SCOPE("ExportTurn::handed");
    if (slot == slots_[turn_]) turn_ = (turn_ + 1u) % images;
}

uint32_t ExportTurn::shown_as(uint32_t slot) const
{
    AV_SCOPE("ExportTurn::shown_as");
    return slot == slots_[1] ? 1u : 0u;
}

bool ExportTurn::invariants_hold() const
{
    AV_SCOPE("ExportTurn::invariants_hold");
    return turn_ < images && slots_[0] != slots_[1];
}

}
