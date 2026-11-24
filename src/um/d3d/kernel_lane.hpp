#ifndef AV_UM_KERNEL_LANE_HPP
#define AV_UM_KERNEL_LANE_HPP

#include "core/support/object_storage.hpp"
#include "core/support/status.hpp"
#include "guest/transport/kmt_blob_arena.hpp"
#include "guest/transport/kmt_transport.hpp"
#include "guest/transport/release_route.hpp"
#include "icd/common/lane_progress.hpp"
#include "icd/d3d/d3d_command_stream.hpp"
#include "um/d3d/d3d11_ddi.hpp"

#include <stddef.h>
#include <stdint.h>

namespace av {

class KeyedLog;

}

namespace av::d3d {

class D3dSession;

class KernelLane final : public ReleaseRoute, public icd::LaneProgress {
public:
    static constexpr uint8_t  ring_order = 20u;
    static constexpr uint32_t reply_bytes = 64u * 1024u;
    static constexpr size_t   staging_bytes = 128u * 1024u;
    static constexpr size_t   batch_bytes = 256u * 1024u;
    static constexpr uint64_t arena_bytes = uint64_t{1} << 20u;

    KernelLane(const D3DDDI_DEVICECALLBACKS &kernel, HANDLE device, HANDLE context, D3dSession &session,
               KeyedLog &lines);
    ~KernelLane();
    KernelLane(const KernelLane &) = delete;
    KernelLane &operator=(const KernelLane &) = delete;

    [[nodiscard]] bool open();
    bool               opened() const;
    HANDLE             context() const;
    D3dCommandStream  &stream();
    bool               ran(uint64_t fence) const override;
    void               publish(uint64_t fence) override;
    uint64_t           newest() const;
    uint64_t           covering() const override;
    void               writing(D3DKMT_HANDLE allocation);
    Status             release(const uint8_t *record, uint32_t size) override;
    void               waited_long() override;

private:
    void close();

    const D3DDDI_DEVICECALLBACKS   &kernel_;
    HANDLE                          device_;
    HANDLE                          context_;
    D3dSession                     &session_;
    KeyedLog                       &lines_;
    ObjectStorage<KmtTransport>     transport_;
    ObjectStorage<KmtBlobArena>     arena_;
    ObjectStorage<D3dCommandStream> stream_;
    uint8_t                        *staging_;
    uint8_t                        *batch_;
    D3DKMT_HANDLE                   written_;
    uint32_t                        declared_;
    uint32_t                        refused_;
    bool                            tried_;
};

}

#endif
