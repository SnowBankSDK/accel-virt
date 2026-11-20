#include "icd/d3d/completion_watermark.hpp"

#include "core/trace/function_scope.hpp"

namespace av::d3d {

CompletionWatermark::CompletionWatermark() : order_(0u), watermark_(0u)
{
    AV_SCOPE("CompletionWatermark::CompletionWatermark");
}

uint64_t CompletionWatermark::stamp_write()
{
    AV_SCOPE("CompletionWatermark::stamp_write");
    return __atomic_add_fetch(&order_, 1u, __ATOMIC_RELAXED);
}

uint64_t CompletionWatermark::ended() const
{
    AV_SCOPE("CompletionWatermark::ended");
    return __atomic_load_n(&order_, __ATOMIC_RELAXED);
}

void CompletionWatermark::seen(uint64_t event_order)
{
    AV_SCOPE("CompletionWatermark::seen");
    uint64_t now = __atomic_load_n(&watermark_, __ATOMIC_RELAXED);
    while (event_order > now &&
           !__atomic_compare_exchange_n(&watermark_, &now, event_order, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

bool CompletionWatermark::known_done(uint64_t write_order) const
{
    AV_SCOPE("CompletionWatermark::known_done");
    return write_order != 0u && write_order <= __atomic_load_n(&watermark_, __ATOMIC_RELAXED);
}

}
