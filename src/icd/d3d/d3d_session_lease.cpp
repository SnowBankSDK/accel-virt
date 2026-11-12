#include "icd/d3d/d3d_session_lease.hpp"

#include "core/trace/function_scope.hpp"
#include "icd/d3d/d3d_command_stream.hpp"

namespace av::d3d {

D3dSessionLease::D3dSessionLease(D3dCommandStream &stream, ThreadLock &lock, const uint64_t *initial)
    : transport_(stream, lock), lease_()
{
    AV_SCOPE("D3dSessionLease::D3dSessionLease");
    lease_.reset(initial);
}

bool D3dSessionLease::take(uint64_t bytes)
{
    AV_SCOPE("D3dSessionLease::take");
    return lease_.take(transport_, LeaseRecord::Kind::device, bytes);
}

void D3dSessionLease::give(uint64_t bytes)
{
    AV_SCOPE("D3dSessionLease::give");
    lease_.give(transport_, LeaseRecord::Kind::device, bytes);
}

uint64_t D3dSessionLease::left() const
{
    AV_SCOPE("D3dSessionLease::left");
    return lease_.left(LeaseRecord::Kind::device);
}

uint64_t D3dSessionLease::extensions() const
{
    AV_SCOPE("D3dSessionLease::extensions");
    return lease_.extensions();
}

}
