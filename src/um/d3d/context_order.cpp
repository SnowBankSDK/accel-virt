#include "um/d3d/context_order.hpp"

#include "core/trace/function_scope.hpp"
#include "guest/transport/kmt_transport.hpp"
#include "um/d3d/d3d_session.hpp"

namespace av::d3d {

ContextOrder::ContextOrder(const D3DDDI_DEVICECALLBACKS &kernel, HANDLE device, HANDLE context, D3dSession &session,
                           KeyedLog &lines, uint32_t channel_device)
    : context_(context), lane_(kernel, device, context, session, lines),
      follower_(kernel, device, session.transport()),
      channel_(session.stream(), session.lock(), channel_device)
{
    AV_SCOPE("ContextOrder::ContextOrder");
}

bool ContextOrder::engage()
{
    AV_SCOPE("ContextOrder::engage");
    if (engaged()) return true;
    if (!lane_.open()) return false;
    return channel_.move_to(lane_.stream()) == Status::ok;
}

bool ContextOrder::engaged() const
{
    AV_SCOPE("ContextOrder::engaged");
    return lane_.opened();
}

D3dDeviceChannel &ContextOrder::wire()
{
    AV_SCOPE("ContextOrder::wire");
    return channel_;
}

KernelLane &ContextOrder::lane()
{
    AV_SCOPE("ContextOrder::lane");
    return lane_;
}

HRESULT ContextOrder::settle()
{
    AV_SCOPE("ContextOrder::settle");
    if (engaged()) return S_OK;
    return follower_.follow(context_);
}

}
