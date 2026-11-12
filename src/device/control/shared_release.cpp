#include "device/control/shared_release.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_plane.hpp"
#include "device/control/held_requests.hpp"
#include "device/export/export_service.hpp"
#include "device/host/host.hpp"

#include <new>

namespace av {

Status SharedRelease::hand_off(Host &host, ContextPlane &plane, ServiceThread &service, const uint8_t *gpu_uuid,
                               uint64_t object_id, const Answer &answer)
{
    AV_SCOPE("SharedRelease::hand_off");
    host.require_main_loop("SharedRelease::hand_off");
    void *const block = host.allocate(sizeof(SharedRelease), Pool::paged);
    if (block == nullptr) return Status::no_memory;
    SharedRelease *const release = ::new (block) SharedRelease(host, plane, gpu_uuid, object_id, answer);
    if (answer.held == nullptr && answer.exports == nullptr) release->detach();
    const Status         handed = plane.hand_off(*release, service);
    if (handed == Status::ok) return Status::ok;
    release->~SharedRelease();
    host.release(block);
    return handed;
}

SharedRelease::SharedRelease(Host &host, ContextPlane &plane, const uint8_t *gpu_uuid, uint64_t object_id,
                             const Answer &answer)
    : host_(host), plane_(plane), gpu_uuid_(gpu_uuid), object_id_(object_id), answer_(answer)
{
    AV_SCOPE("SharedRelease::SharedRelease");
}

void SharedRelease::run()
{
    AV_SCOPE("SharedRelease::run");
    plane_.shared_released(gpu_uuid_, object_id_);
}

void SharedRelease::finish(bool ran)
{
    AV_SCOPE("SharedRelease::finish");
    if (answer_.exports != nullptr) answer_.exports->revoke(object_id_, ExportRevocation::released);
    if (answer_.held != nullptr) answer_.held->conclude(answer_.ticket, ran ? Status::ok : Status::invalid_state);
    Host &host = host_;
    this->~SharedRelease();
    host.release(this);
}

}
