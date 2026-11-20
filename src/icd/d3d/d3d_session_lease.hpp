#ifndef AV_D3D_SESSION_LEASE_HPP
#define AV_D3D_SESSION_LEASE_HPP

#include "guest/transport/memory_lease.hpp"
#include "icd/common/locked_transport.hpp"

#include <stdint.h>

namespace av {
class ThreadLock;
}

namespace av::d3d {

class D3dCommandStream;

class D3dSessionLease {
public:
    D3dSessionLease(D3dCommandStream &stream, ThreadLock &lock, const uint64_t *initial);
    D3dSessionLease(const D3dSessionLease &) = delete;
    D3dSessionLease &operator=(const D3dSessionLease &) = delete;

    [[nodiscard]] bool take(uint64_t bytes);
    void               give(uint64_t bytes);
    uint64_t           left() const;
    uint64_t           extensions() const;

private:
    icd::LockedTransport transport_;
    MemoryLease          lease_;
};

}

#endif
