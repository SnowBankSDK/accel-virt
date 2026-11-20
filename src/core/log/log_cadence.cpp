#include "core/log/log_cadence.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

LogCadence::LogCadence() : seen_(0u)
{
    AV_SCOPE("LogCadence::LogCadence");
}

bool LogCadence::speaks()
{
    AV_SCOPE("LogCadence::speaks");
    const uint64_t seen = __atomic_add_fetch(&seen_, 1u, __ATOMIC_RELAXED);
    return seen <= spoken_first || seen % then_every == 0u;
}

uint64_t LogCadence::seen() const
{
    AV_SCOPE("LogCadence::seen");
    return __atomic_load_n(&seen_, __ATOMIC_RELAXED);
}

void LogCadence::reset()
{
    AV_SCOPE("LogCadence::reset");
    __atomic_store_n(&seen_, 0u, __ATOMIC_RELAXED);
}

}
