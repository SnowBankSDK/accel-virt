#ifndef AV_SHARED_RELEASE_HPP
#define AV_SHARED_RELEASE_HPP

#include "core/support/status.hpp"
#include "device/threading/errand.hpp"

#include <stdint.h>

namespace av {

class ContextPlane;
class ExportService;
class HeldRequests;
class Host;
class ServiceThread;

class SharedRelease final : public Errand {
public:
    struct Answer {
        HeldRequests  *held;
        ExportService *exports;
        uint32_t       ticket;
    };

    [[nodiscard]] static Status hand_off(Host &host, ContextPlane &plane, ServiceThread &service,
                                         const uint8_t *gpu_uuid, uint64_t object_id, const Answer &answer);

    SharedRelease(Host &host, ContextPlane &plane, const uint8_t *gpu_uuid, uint64_t object_id, const Answer &answer);
    ~SharedRelease() = default;

protected:
    void run() override;
    void finish(bool ran) override;

private:
    Host          &host_;
    ContextPlane  &plane_;
    const uint8_t *gpu_uuid_;
    uint64_t       object_id_;
    Answer         answer_;
};

}

#endif
