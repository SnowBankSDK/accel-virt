#ifndef AV_D3D_PAGE_LENDER_HPP
#define AV_D3D_PAGE_LENDER_HPP

#include "core/support/status.hpp"

#include <stdint.h>

namespace av {
class HostAllocation;
}

namespace av::d3d {

class PageLender {
public:
    PageLender(const PageLender &) = delete;
    PageLender &operator=(const PageLender &) = delete;

    [[nodiscard]] virtual Status lend(uint32_t allocation, uint64_t bytes, HostAllocation *&made) const = 0;

protected:
    PageLender() = default;
    ~PageLender() = default;
};

}

#endif
