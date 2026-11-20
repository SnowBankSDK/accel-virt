#include "device/memory/host_memory_free.hpp"

#include "core/trace/function_scope.hpp"
#include "device/context/context_plane.hpp"
#include "device/host/host.hpp"

#include <new>

namespace av {

Status HostMemoryFree::hand_off(Host &host, ContextPlane &plane, ServiceThread &service, uint64_t key)
{
    AV_SCOPE("HostMemoryFree::hand_off");
    void *const block = host.allocate(sizeof(HostMemoryFree), Pool::paged);
    if (block == nullptr) return Status::no_memory;
    HostMemoryFree *const free = ::new (block) HostMemoryFree(host, plane, key);
    free->detach();
    const Status handed = plane.hand_off(*free, service);
    if (handed == Status::ok) return Status::ok;
    free->~HostMemoryFree();
    host.release(block);
    return handed;
}

HostMemoryFree::HostMemoryFree(Host &host, ContextPlane &plane, uint64_t key) : host_(host), plane_(plane), key_(key)
{
    AV_SCOPE("HostMemoryFree::HostMemoryFree");
}

void HostMemoryFree::run()
{
    AV_SCOPE("HostMemoryFree::run");
    plane_.free_memory(key_);
}

void HostMemoryFree::finish(bool)
{
    AV_SCOPE("HostMemoryFree::finish");
    Host &host = host_;
    this->~HostMemoryFree();
    host.release(this);
}

}
