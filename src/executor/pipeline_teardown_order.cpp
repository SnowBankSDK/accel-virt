#include "executor/pipeline_teardown_order.hpp"

#include "core/trace/function_scope.hpp"

namespace av {

namespace {

int order_needed_by(void *self, uint32_t driver_id)
{
    AV_SCOPE("order_needed_by");
    return static_cast<PipelineTeardownOrder *>(self)->needed_by(static_cast<VkDriverId>(driver_id)) ? 1 : 0;
}

void order_compiling(void *self)
{
    AV_SCOPE("order_compiling");
    static_cast<PipelineTeardownOrder *>(self)->compiling();
}

void order_compiled(void *self)
{
    AV_SCOPE("order_compiled");
    static_cast<PipelineTeardownOrder *>(self)->compiled();
}

void order_destroying(void *self)
{
    AV_SCOPE("order_destroying");
    static_cast<PipelineTeardownOrder *>(self)->destroying();
}

void order_destroyed(void *self)
{
    AV_SCOPE("order_destroyed");
    static_cast<PipelineTeardownOrder *>(self)->destroyed();
}

}

PipelineTeardownOrder::PipelineTeardownOrder() : abi_{}, lock_(), destroys_(0u)
{
    AV_SCOPE("PipelineTeardownOrder::PipelineTeardownOrder");
    abi_.size = sizeof(AvTeardownOrder);
    abi_.self = this;
    abi_.needed_by = order_needed_by;
    abi_.compiling = order_compiling;
    abi_.compiled = order_compiled;
    abi_.destroying = order_destroying;
    abi_.destroyed = order_destroyed;
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

const AvTeardownOrder &PipelineTeardownOrder::abi() const
{
    AV_SCOPE("PipelineTeardownOrder::abi");
    return abi_;
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
