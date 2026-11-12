#include "um/d3d/context_order.hpp"

#include "core/trace/function_scope.hpp"
#include "guest/transport/kmt_transport.hpp"
#include "um/d3d/d3d_session.hpp"

namespace av::d3d {

ContextOrder::ContextOrder(const D3DDDI_DEVICECALLBACKS &kernel, HANDLE device, HANDLE context, D3dSession &session,
                           KeyedLog &lines, uint32_t channel_device)
    : context_(context), route_(kernel, device, session.transport(), session.lock(), lines, context),
      follower_(kernel, device, session.transport()),
      channel_(session.stream(), session.lock(), channel_device, &route_)
{
    AV_SCOPE("ContextOrder::ContextOrder");
}

void ContextOrder::engage()
{
    AV_SCOPE("ContextOrder::engage");
    if (!route_.engaged()) route_.engage();
}

bool ContextOrder::engaged() const
{
    AV_SCOPE("ContextOrder::engaged");
    return route_.engaged();
}

D3dDeviceChannel &ContextOrder::wire()
{
    AV_SCOPE("ContextOrder::wire");
    return channel_;
}

HRESULT ContextOrder::settle()
{
    AV_SCOPE("ContextOrder::settle");
    if (route_.engaged() && route_.routes()) return S_OK;
    return follower_.follow(context_);
}

}
