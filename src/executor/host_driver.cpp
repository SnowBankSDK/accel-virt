#include "executor/host_driver.hpp"

#include "core/trace/function_scope.hpp"

#include <string.h>

namespace av {

HostDriver::HostDriver() : id_(static_cast<VkDriverId>(0)), name_{}, build_{}
{
    AV_SCOPE("HostDriver::HostDriver");
}

HostDriver::HostDriver(VkDriverId id, const char *name, const char *build) : id_(id), name_{}, build_{}
{
    AV_SCOPE("HostDriver::HostDriver");
    if (name != nullptr) strncpy(name_, name, sizeof(name_) - 1u);
    if (build != nullptr) strncpy(build_, build, sizeof(build_) - 1u);
}

VkDriverId HostDriver::id() const
{
    AV_SCOPE("HostDriver::id");
    return id_;
}

bool HostDriver::same_library(const HostDriver &other) const
{
    AV_SCOPE("HostDriver::same_library");
    return id_ == other.id_ && strcmp(name_, other.name_) == 0 && strcmp(build_, other.build_) == 0;
}

}
