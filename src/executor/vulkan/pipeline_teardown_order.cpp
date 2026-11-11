#include "executor/vulkan/pipeline_teardown_order.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

PipelineTeardownOrder::PipelineTeardownOrder() : lock_(), destroys_(0u)
{
    AV_SCOPE("PipelineTeardownOrder::PipelineTeardownOrder");
    pthread_rwlockattr_t attributes;
    pthread_rwlockattr_init(&attributes);
    pthread_rwlockattr_setkind_np(&attributes, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    pthread_rwlock_init(&lock_, &attributes);
    pthread_rwlockattr_destroy(&attributes);
}

PipelineTeardownOrder::~PipelineTeardownOrder()
{
    AV_SCOPE("PipelineTeardownOrder::~PipelineTeardownOrder");
    pthread_rwlock_destroy(&lock_);
}

bool PipelineTeardownOrder::needed_by(VkDriverId driver) const
{
    AV_SCOPE("PipelineTeardownOrder::needed_by");
    return driver == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
}

void PipelineTeardownOrder::compiling()
{
    AV_SCOPE("PipelineTeardownOrder::compiling");
    pthread_rwlock_rdlock(&lock_);
}

void PipelineTeardownOrder::compiled()
{
    AV_SCOPE("PipelineTeardownOrder::compiled");
    pthread_rwlock_unlock(&lock_);
}

void PipelineTeardownOrder::destroying()
{
    AV_SCOPE("PipelineTeardownOrder::destroying");
    pthread_rwlock_wrlock(&lock_);
    destroys_++;
}

void PipelineTeardownOrder::destroyed()
{
    AV_SCOPE("PipelineTeardownOrder::destroyed");
    pthread_rwlock_unlock(&lock_);
}

uint64_t PipelineTeardownOrder::destroys() const
{
    AV_SCOPE("PipelineTeardownOrder::destroys");
    return __atomic_load_n(&destroys_, __ATOMIC_RELAXED);
}

}
