#ifndef AV_UM_CONTEXT_ORDER_HPP
#define AV_UM_CONTEXT_ORDER_HPP

#include "icd/d3d/d3d_device_channel.hpp"
#include "um/d3d/batch_follower.hpp"
#include "um/d3d/packet_release.hpp"

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

    void              engage();
    bool              engaged() const;
    D3dDeviceChannel &wire();
    [[nodiscard]] HRESULT settle();

private:
    HANDLE           context_;
    PacketRelease    route_;
    BatchFollower    follower_;
    D3dDeviceChannel channel_;
};

}

#endif
