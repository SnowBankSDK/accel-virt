#include "icd/d3d/cross_adapter_rows.hpp"

#include "core/trace/function_scope.hpp"
#include "icd/d3d/kernel_surface.hpp"
#include "icd/d3d/page_lender.hpp"
#include "icd/d3d/surface_allocations.hpp"
#include "proto/resources/allocation_descriptor.hpp"
#include "proto/resources/allocation_kind.hpp"

namespace av::d3d {

CrossAdapterRows::CrossAdapterRows(SurfaceAllocations &allocations, const PageLender &lender)
    : allocations_(allocations), lender_(lender), allocation_(0u), pitch_(0u), key_(0u), lent_(nullptr)
{
    AV_SCOPE("CrossAdapterRows::CrossAdapterRows");
}

Hresult CrossAdapterRows::make(uint32_t width, uint32_t height, uint32_t dxgi_format, void *resource)
{
    AV_SCOPE("CrossAdapterRows::make");
    KernelSurface surface(SharedWith::another_adapter, width, height, dxgi_format);
    if (surface.make() != Status::ok) return hr_invalid_arg;
    uint32_t      allocation = 0u;
    const Hresult made = allocations_.make_surface(surface, resource, allocation);
    if (made < 0) return made;
    HostAllocation *lent = nullptr;
    if (lender_.lend(allocation, surface.bytes(), lent) != Status::ok || lent == nullptr) {
        allocations_.return_surface(allocation, resource);
        return hr_out_of_memory;
    }
    allocation_ = allocation;
    pitch_ = surface.pitch();
    key_ = surface.key();
    lent_ = lent;
    return hr_ok;
}

Hresult CrossAdapterRows::adopt(uint32_t allocation, const AllocationDescriptor &descriptor)
{
    AV_SCOPE("CrossAdapterRows::adopt");
    if (allocation == 0u || descriptor.kind() != AllocationKind::cross_adapter || descriptor.layout().pitch() == 0u ||
        descriptor.size() == 0u)
        return hr_invalid_arg;
    HostAllocation *lent = nullptr;
    if (lender_.lend(allocation, descriptor.size(), lent) != Status::ok || lent == nullptr) return hr_out_of_memory;
    allocation_ = allocation;
    pitch_ = descriptor.layout().pitch();
    key_ = 0u;
    lent_ = lent;
    return hr_ok;
}

uint32_t CrossAdapterRows::allocation() const
{
    AV_SCOPE("CrossAdapterRows::allocation");
    return allocation_;
}

uint32_t CrossAdapterRows::pitch() const
{
    AV_SCOPE("CrossAdapterRows::pitch");
    return pitch_;
}

uint64_t CrossAdapterRows::key() const
{
    AV_SCOPE("CrossAdapterRows::key");
    return key_;
}

HostAllocation *CrossAdapterRows::lent() const
{
    AV_SCOPE("CrossAdapterRows::lent");
    return lent_;
}

}
