#ifndef AV_D3D_SURFACE_ALLOCATIONS_HPP
#define AV_D3D_SURFACE_ALLOCATIONS_HPP

#include "icd/d3d/hresult.hpp"

#include <stdint.h>

namespace av::d3d {

class KernelSurface;

class SurfaceAllocations {
public:
    SurfaceAllocations(const SurfaceAllocations &) = delete;
    SurfaceAllocations &operator=(const SurfaceAllocations &) = delete;

    [[nodiscard]] virtual Hresult make_surface(KernelSurface &surface, void *resource, uint32_t &allocation) = 0;
    virtual void                  return_surface(uint32_t allocation, void *resource) = 0;

protected:
    SurfaceAllocations() = default;
    ~SurfaceAllocations() = default;
};

}

#endif
