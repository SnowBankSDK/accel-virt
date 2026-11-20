#include "device/control/present_engine_commands.hpp"

#include "core/support/text_buffer.hpp"
#include "core/trace/function_scope.hpp"
#include "device/control/control_request.hpp"
#include "device/control/held_requests.hpp"
#include "device/host/host.hpp"
#include "device/present/present_engine.hpp"
#include "proto/virtio_gpu/gpu_response.hpp"
#include "proto/virtio_gpu/page_request.hpp"
#include "proto/virtio_gpu/queued_present_request.hpp"
#include "proto/wire/wire_reader.hpp"

namespace av {

PresentEngineCommands::PresentEngineCommands(Host &host, PresentEngine &engine, const PresentFrames &snapshots,
                                             HeldRequests &held)
    : host_(host), engine_(engine), snapshots_(snapshots), held_(held)
{
    AV_SCOPE("PresentEngineCommands::PresentEngineCommands");
}

bool PresentEngineCommands::answer(ControlRequest &request, uint32_t type)
{
    AV_SCOPE("PresentEngineCommands::answer");
    host_.require_main_loop("PresentEngineCommands::answer");
    if (type == PageRequest::present_engine) {
        register_page(request);
        return true;
    }
    if (type != QueuedPresentRequest::type) return false;
    GpuResponse response;
    response.set(engine_.queue(request));
    request.respond(response);
    return true;
}

void PresentEngineCommands::register_page(ControlRequest &request)
{
    AV_SCOPE("PresentEngineCommands::register_page");
    uint8_t      bytes[PageRequest::encoded_size] = {};
    WireReader   reader(bytes, request.read(0u, bytes, sizeof bytes));
    PageRequest  page;
    GpuResponse  response;
    const bool   decoded = page.decode(reader) == Status::ok;
    uint32_t     ticket = HeldRequests::no_ticket;
    if (decoded) engine_.detach();
    if (decoded && engine_.releasing()) {
        if (held_.hold(request, &PresentEngineCommands::attach_answer, ticket) == Status::ok) {
            engine_.attach_when_released(ticket, page.address(), snapshots_);
            return;
        }
    }
    const Status attached = decoded ? engine_.attach(page.address(), snapshots_) : Status::malformed;
    response.set(attach_answer(attached));
    if (attached != Status::ok) {
        char       line[112] = {};
        TextBuffer text(line, sizeof line);
        text.append("virtio-gpu-av: the present engine's page was refused, status ");
        text.append_decimal(static_cast<uint32_t>(attached));
        host_.log(text.text());
    }
    request.respond(response);
}

uint32_t PresentEngineCommands::attach_answer(Status status)
{
    AV_SCOPE("PresentEngineCommands::attach_answer");
    if (status == Status::no_memory) return GpuResponse::err_out_of_memory;
    return status == Status::ok ? GpuResponse::ok_nodata : GpuResponse::err_invalid_parameter;
}

}
