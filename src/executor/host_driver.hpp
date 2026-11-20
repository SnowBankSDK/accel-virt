#ifndef AV_HOST_DRIVER_HPP
#define AV_HOST_DRIVER_HPP

#include <vulkan/vulkan_core.h>

namespace av {

class HostDriver {
public:
    HostDriver();
    HostDriver(VkDriverId id, const char *name, const char *build);

    VkDriverId id() const;
    bool       same_library(const HostDriver &other) const;

private:
    VkDriverId id_;
    char       name_[VK_MAX_DRIVER_NAME_SIZE];
    char       build_[VK_MAX_DRIVER_INFO_SIZE];
};

}

#endif
