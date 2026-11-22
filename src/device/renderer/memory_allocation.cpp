#include "device/renderer/memory_allocation.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_plane.hpp"
#include "device/renderer/renderer_service.hpp"

namespace av {

MemoryAllocation::MemoryAllocation(RendererService &service, ContextPlane &plane, uint64_t sequence,
                                   const AllocateMemoryMessage &request)
    : service_(service), plane_(plane), sequence_(sequence), request_(request), fate_(Fate::kept),
      status_(Status::invalid_state), memory_{-1, 0u, 0u}
{
    AV_SCOPE("MemoryAllocation::MemoryAllocation");
}

void MemoryAllocation::run()
{
    AV_SCOPE("MemoryAllocation::run");
    status_ = plane_.allocate_memory(request_.context(), request_.key(), request_.size(), request_.request(), memory_);
}

void MemoryAllocation::finish(bool ran)
{
    AV_SCOPE("MemoryAllocation::finish");
    service_.allocated(*this, ran);
}

void MemoryAllocation::forsake()
{
    AV_SCOPE("MemoryAllocation::forsake");
    fate_ = Fate::forsaken;
}

void MemoryAllocation::disown()
{
    AV_SCOPE("MemoryAllocation::disown");
    fate_ = Fate::disowned;
}

bool MemoryAllocation::forsaken() const
{
    AV_SCOPE("MemoryAllocation::forsaken");
    return fate_ != Fate::kept;
}

bool MemoryAllocation::frees_itself() const
{
    AV_SCOPE("MemoryAllocation::frees_itself");
    return fate_ != Fate::forsaken;
}

uint64_t MemoryAllocation::sequence() const
{
    AV_SCOPE("MemoryAllocation::sequence");
    return sequence_;
}

const AllocateMemoryMessage &MemoryAllocation::request() const
{
    AV_SCOPE("MemoryAllocation::request");
    return request_;
}

Status MemoryAllocation::status() const
{
    AV_SCOPE("MemoryAllocation::status");
    return status_;
}

const AvHostMemory &MemoryAllocation::memory() const
{
    AV_SCOPE("MemoryAllocation::memory");
    return memory_;
}

}
