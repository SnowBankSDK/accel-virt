#ifndef AV_ICD_IMAGE_FORMAT_CHAIN_HPP
#define AV_ICD_IMAGE_FORMAT_CHAIN_HPP

#include <stdint.h>

#include <vulkan/vulkan_core.h>

namespace av::icd {
class Win32HandleKind;

class ImageFormatChain {
public:
    enum class Outcome { untouched, translated, refused };

    ImageFormatChain();
    ImageFormatChain(const ImageFormatChain &) = delete;
    ImageFormatChain &operator=(const ImageFormatChain &) = delete;

    [[nodiscard]] Outcome take(const VkPhysicalDeviceImageFormatInfo2 &info, const Win32HandleKind &kind,
                               VkFlags share_type);
    const VkPhysicalDeviceImageFormatInfo2 *forwarded() const;
    VkFlags                                 asked_host_type() const;


    void report(VkImageFormatProperties2 &answer, const Win32HandleKind &kind) const;

private:
    void    link(void *node);
    void    clear();
    Outcome refusal();

    VkPhysicalDeviceImageFormatInfo2            info_;
    VkPhysicalDeviceExternalImageFormatInfo     external_;
    VkImageFormatListCreateInfo                 list_;
    VkImageStencilUsageCreateInfo               stencil_;
    VkPhysicalDeviceImageViewImageFormatInfoEXT view_;
    VkBaseOutStructure                         *last_;
};

}

#endif
