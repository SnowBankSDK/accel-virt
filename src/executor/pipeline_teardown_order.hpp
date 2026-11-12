#ifndef AV_PIPELINE_TEARDOWN_ORDER_HPP
#define AV_PIPELINE_TEARDOWN_ORDER_HPP

#include "executor/av_teardown_order.h"

#include <pthread.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

namespace av {

class PipelineTeardownOrder {
public:
    PipelineTeardownOrder();
    ~PipelineTeardownOrder();
    PipelineTeardownOrder(const PipelineTeardownOrder &) = delete;
    PipelineTeardownOrder &operator=(const PipelineTeardownOrder &) = delete;

    const AvTeardownOrder &abi() const;
    bool needed_by(VkDriverId driver) const;
    void compiling();
    void compiled();
    void destroying();
    void destroyed();
    uint64_t destroys() const;

private:
    AvTeardownOrder  abi_;
    pthread_rwlock_t lock_;
    uint64_t         destroys_;
};

}

#endif
