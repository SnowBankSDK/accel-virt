#include "icd/d3d/amortized_processing.hpp"

#include "core/trace/function_scope.hpp"
#include "icd/common/calling_thread.hpp"
#include "icd/d3d/reentrant_call_lock.hpp"

namespace av::d3d {

AmortizedProcessing::AmortizedProcessing(ReentrantCallLock &calls, icd::CallingThread &caller)
    : calls_(calls), caller_(caller), counted_(0u), amortizing_(0u)
{
    AV_SCOPE("AmortizedProcessing::AmortizedProcessing");
}

bool AmortizedProcessing::due(uint64_t count, uint64_t step)
{
    AV_SCOPE("AmortizedProcessing::due");
    if (!calls_.outermost() || __atomic_load_n(&amortizing_, __ATOMIC_ACQUIRE) == caller_.thread_id()) return false;
    if (count - counted_ < step) return false;
    counted_ = count;
    return true;
}

void AmortizedProcessing::begin()
{
    AV_SCOPE("AmortizedProcessing::begin");
    __atomic_store_n(&amortizing_, caller_.thread_id(), __ATOMIC_RELEASE);
}

void AmortizedProcessing::end()
{
    AV_SCOPE("AmortizedProcessing::end");
    __atomic_store_n(&amortizing_, 0u, __ATOMIC_RELEASE);
}

}
