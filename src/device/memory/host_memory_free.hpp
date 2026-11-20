#ifndef AV_HOST_MEMORY_FREE_HPP
#define AV_HOST_MEMORY_FREE_HPP

#include "core/support/status.hpp"
#include "device/threading/errand.hpp"

#include <stdint.h>

namespace av {

class ContextPlane;
class Host;
class ServiceThread;

class HostMemoryFree final : public Errand {
public:
    [[nodiscard]] static Status hand_off(Host &host, ContextPlane &plane, ServiceThread &service, uint64_t key);

    HostMemoryFree(Host &host, ContextPlane &plane, uint64_t key);
    ~HostMemoryFree() = default;

protected:
    void run() override;
    void finish(bool ran) override;

private:
    Host         &host_;
    ContextPlane &plane_;
    uint64_t      key_;
};

}

#endif
