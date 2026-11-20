#include "device/control/shared_commands.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_plane.hpp"
#include "device/control/control_request.hpp"
#include "device/control/held_requests.hpp"
#include "device/control/shared_release.hpp"
#include "device/export/export_service.hpp"
#include "device/host/host.hpp"
#include "proto/virtio_gpu/gpu_response.hpp"
#include "proto/virtio_gpu/shared_release_request.hpp"
#include "proto/wire/wire_reader.hpp"

namespace av {

SharedCommands::SharedCommands(Host &host, ContextPlane &contexts, ExportService &exports, const uint8_t *gpu_uuid,
                               ServiceThread &service, HeldRequests &held)
    : host_(host), contexts_(contexts), exports_(exports), gpu_uuid_(gpu_uuid), service_(service), held_(held)
{
    AV_SCOPE("SharedCommands::SharedCommands");
}

bool SharedCommands::answer(ControlRequest &request, uint32_t type)
{
    AV_SCOPE("SharedCommands::answer");
    host_.require_main_loop("SharedCommands::answer");
    if (type != SharedReleaseRequest::type) return false;
    uint8_t              bytes[SharedReleaseRequest::encoded_size] = {};
    WireReader           reader(bytes, request.read(0u, bytes, sizeof bytes));
    SharedReleaseRequest release;
    GpuResponse          response;
    uint32_t             ticket = HeldRequests::no_ticket;
    if (release.decode(reader) != Status::ok) {
        response.set(GpuResponse::err_invalid_parameter);
    } else if (held_.hold(request, &SharedCommands::release_answer, ticket) != Status::ok) {
        response.set(GpuResponse::err_out_of_memory);
    } else {
        const SharedRelease::Answer answer = {&held_, &exports_, ticket};
        const Status handed =
            SharedRelease::hand_off(host_, contexts_, service_, gpu_uuid_, release.object_id(), answer);
        if (handed != Status::ok) held_.conclude(ticket, handed);
    }
    request.respond(response);
    return true;
}

uint32_t SharedCommands::release_answer(Status status)
{
    AV_SCOPE("SharedCommands::release_answer");
    return status == Status::ok ? GpuResponse::ok_nodata : GpuResponse::err_out_of_memory;
}

}
