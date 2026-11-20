#ifndef AV_PRESENT_ENGINE_COMMANDS_HPP
#define AV_PRESENT_ENGINE_COMMANDS_HPP

#include "core/support/status.hpp"

#include <stdint.h>

namespace av {

class ControlRequest;
class HeldRequests;
class Host;
class PresentEngine;
class PresentFrames;

class PresentEngineCommands {
public:
    PresentEngineCommands(Host &host, PresentEngine &engine, const PresentFrames &snapshots, HeldRequests &held);
    PresentEngineCommands(const PresentEngineCommands &) = delete;
    PresentEngineCommands &operator=(const PresentEngineCommands &) = delete;

    bool answer(ControlRequest &request, uint32_t type);

    static uint32_t attach_answer(Status status);

private:
    void register_page(ControlRequest &request);

    Host                   &host_;
    PresentEngine          &engine_;
    const PresentFrames &snapshots_;
    HeldRequests        &held_;
};

}

#endif
