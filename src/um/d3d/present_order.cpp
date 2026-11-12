#include "um/d3d/present_order.hpp"

#include "core/trace/function_scope.hpp"
#include "guest/transport/kmt_transport.hpp"

namespace av::d3d {

namespace {

constexpr DWORD bound_ms = 1000u;

}

PresentOrder::PresentOrder(const D3DDDI_DEVICECALLBACKS &kernel, HANDLE device, HANDLE context,
                           KmtTransport &transport, KeyedLog &lines, const KernelLane &lane)
    : context_(context), lane_(lane), transport_(transport), follower_(kernel, device, transport),
      gate_(kernel, device, context, lines, bound_ms)
{
    AV_SCOPE("PresentOrder::PresentOrder");
}

PresentOrder::~PresentOrder()
{
    AV_SCOPE("PresentOrder::~PresentOrder");
    transport_.forget(gate_);
}

HRESULT PresentOrder::follow()
{
    AV_SCOPE("PresentOrder::follow");
    if (lane_.opened()) return S_OK;
    return follower_.follow(context_);
}

HRESULT PresentOrder::submitted(HRESULT status, Reuse reuse)
{
    AV_SCOPE("PresentOrder::submitted");
    if (SUCCEEDED(status) && reuse == Reuse::after_context && !lane_.opened())
        transport_.hold(gate_);
    return status;
}

}
