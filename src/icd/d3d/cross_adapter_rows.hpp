#ifndef AV_D3D_CROSS_ADAPTER_ROWS_HPP
#define AV_D3D_CROSS_ADAPTER_ROWS_HPP

#include "icd/d3d/hresult.hpp"

#include <stdint.h>

namespace av {
class AllocationDescriptor;
class HostAllocation;
}

namespace av::d3d {

class PageLender;
class SurfaceAllocations;

class CrossAdapterRows {
public:
    CrossAdapterRows(SurfaceAllocations &allocations, const PageLender &lender);
    CrossAdapterRows(const CrossAdapterRows &) = delete;
    CrossAdapterRows &operator=(const CrossAdapterRows &) = delete;

    [[nodiscard]] Hresult make(uint32_t width, uint32_t height, uint32_t dxgi_format, void *resource);
    [[nodiscard]] Hresult adopt(uint32_t allocation, const AllocationDescriptor &descriptor);

    uint32_t        allocation() const;
    uint32_t        pitch() const;
    uint64_t        key() const;
    HostAllocation *lent() const;

private:
    SurfaceAllocations &allocations_;
    const PageLender   &lender_;
    uint32_t            allocation_;
    uint32_t            pitch_;
    uint64_t            key_;
    HostAllocation     *lent_;
};

}

#endif
