#ifndef AV_SHARED_COMMANDS_HPP
#define AV_SHARED_COMMANDS_HPP

#include "core/support/status.hpp"

#include <stdint.h>

namespace av {

class ContextPlane;
class ControlRequest;
class ExportService;
class HeldRequests;
class Host;
class ServiceThread;

class SharedCommands {
public:
    SharedCommands(Host &host, ContextPlane &contexts, ExportService &exports, const uint8_t *gpu_uuid,
                   ServiceThread &service, HeldRequests &held);
    SharedCommands(const SharedCommands &) = delete;
    SharedCommands &operator=(const SharedCommands &) = delete;

    bool answer(ControlRequest &request, uint32_t type);

    static uint32_t release_answer(Status status);

private:
    Host          &host_;
    ContextPlane  &contexts_;
    ExportService &exports_;
    const uint8_t *gpu_uuid_;
    ServiceThread &service_;
    HeldRequests  &held_;
};

}

#endif
