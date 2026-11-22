#ifndef AV_MEMORY_ALLOCATION_HPP
#define AV_MEMORY_ALLOCATION_HPP

#include "core/support/status.hpp"
#include "device/abi/av_executor.h"
#include "device/threading/errand.hpp"
#include "proto/renderer/allocate_memory_message.hpp"

#include <stdint.h>

namespace av {

class ContextPlane;
class RendererService;

class MemoryAllocation final : public Errand {
public:
    MemoryAllocation(RendererService &service, ContextPlane &plane, uint64_t sequence,
                     const AllocateMemoryMessage &request);
    ~MemoryAllocation() = default;

    void                         forsake();
    void                         disown();
    bool                         forsaken() const;
    bool                         frees_itself() const;
    uint64_t                     sequence() const;
    const AllocateMemoryMessage &request() const;
    Status                       status() const;
    const AvHostMemory          &memory() const;

protected:
    void run() override;
    void finish(bool ran) override;

private:
    enum class Fate : uint8_t { kept, forsaken, disowned };

    RendererService      &service_;
    ContextPlane         &plane_;
    uint64_t              sequence_;
    AllocateMemoryMessage request_;
    Fate                  fate_;
    Status                status_;
    AvHostMemory          memory_;
};

}

#endif
