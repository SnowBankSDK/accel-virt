#ifndef AV_UM_CONTEXT_ORDER_HPP
#define AV_UM_CONTEXT_ORDER_HPP

#include "icd/d3d/d3d_device_channel.hpp"
#include "um/d3d/batch_follower.hpp"
#include "um/d3d/kernel_lane.hpp"

#include <stdint.h>

namespace av {

class KeyedLog;

}

namespace av::d3d {

class D3dSession;

class ContextOrder {
public:
    ContextOrder(const D3DDDI_DEVICECALLBACKS &kernel, HANDLE device, HANDLE context, D3dSession &session,
                 KeyedLog &lines, uint32_t channel_device);
    ContextOrder(const ContextOrder &) = delete;
    ContextOrder &operator=(const ContextOrder &) = delete;

    bool              engage();
    bool              engaged() const;
    D3dDeviceChannel &wire();
    KernelLane       &lane();
    [[nodiscard]] HRESULT settle();

private:
    HANDLE           context_;
    KernelLane       lane_;
    BatchFollower    follower_;
    D3dDeviceChannel channel_;
};

}

#endif
