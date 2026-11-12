#include "core/platform/one_shot_timer.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

OneShotTimer::OneShotTimer(Platform &platform, TimerRoutine routine, void *context)
    : platform_(platform), routine_(routine), context_(context), storage_{}
{
    AV_SCOPE("OneShotTimer::OneShotTimer");
    platform_.require_irql_at_most(Irql::passive, "OneShotTimer::OneShotTimer");
    platform_.initialize_timer(storage_, routine_, context_);
}

OneShotTimer::~OneShotTimer()
{
    AV_SCOPE("OneShotTimer::~OneShotTimer");
    platform_.release_timer(storage_);
}

void OneShotTimer::arm(uint64_t ticks)
{
    AV_SCOPE("OneShotTimer::arm");
    platform_.require_irql_at_most(Irql::dispatch, "OneShotTimer::arm");
    platform_.arm_timer(storage_, ticks);
}

void OneShotTimer::cancel()
{
    AV_SCOPE("OneShotTimer::cancel");
    platform_.require_irql_at_most(Irql::dispatch, "OneShotTimer::cancel");
    platform_.cancel_timer(storage_);
}

void OneShotTimer::quiesce()
{
    AV_SCOPE("OneShotTimer::quiesce");
    platform_.require_irql_at_most(Irql::passive, "OneShotTimer::quiesce");
    platform_.release_timer(storage_);
    platform_.initialize_timer(storage_, routine_, context_);
}

}
