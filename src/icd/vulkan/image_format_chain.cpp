#include "icd/vulkan/image_format_chain.hpp"

#include "core/trace/function_scope.hpp"
#include "icd/vulkan/win32_handle_kind.hpp"
#include "proto/wire/foreign_enum.hpp"

namespace av::icd {

namespace {
constexpr uint32_t carried_kinds = 4u;
constexpr uint32_t longest_chain = 64u;

const VkPhysicalDeviceExternalImageFormatInfo *external_of(const VkPhysicalDeviceImageFormatInfo2 &info)
{
    AV_SCOPE("external_of");
    uint32_t read = 0u;
    for (const auto *node = static_cast<const VkBaseInStructure *>(info.pNext); node != nullptr && read < longest_chain;
         node = node->pNext, read++)
        if (static_cast<VkStructureType>(foreign_enum(node->sType)) ==
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO)
            return reinterpret_cast<const VkPhysicalDeviceExternalImageFormatInfo *>(node);
    return nullptr;
}
}

ImageFormatChain::ImageFormatChain() : info_{}, external_{}, list_{}, stencil_{}, view_{}, last_(nullptr)
{
    AV_SCOPE("ImageFormatChain::ImageFormatChain");
}

ImageFormatChain::Outcome ImageFormatChain::take(const VkPhysicalDeviceImageFormatInfo2 &info,
                                                 const Win32HandleKind &kind, VkFlags share_type)
{
    AV_SCOPE("ImageFormatChain::take");
    clear();
    const VkPhysicalDeviceExternalImageFormatInfo *const external = external_of(info);
    if (external == nullptr || external->handleType == 0u) return Outcome::untouched;
    if (!kind.answers(external->handleType) || share_type == 0u) return refusal();
    info_ = info;
    info_.pNext = nullptr;
    uint32_t        read = 0u;
    VkStructureType seen[carried_kinds] = {};
    for (const auto *node = static_cast<const VkBaseInStructure *>(info.pNext); node != nullptr;
         node = node->pNext) {
        const VkStructureType type = static_cast<VkStructureType>(foreign_enum(node->sType));
        for (uint32_t earlier = 0u; earlier < read; earlier++)
            if (seen[earlier] == type) return refusal();
        if (read == carried_kinds) return refusal();
        seen[read++] = type;
        if (type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO) {
            external_.sType = type;
            external_.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(share_type);
            link(&external_);
        } else if (type == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) {
            list_ = *reinterpret_cast<const VkImageFormatListCreateInfo *>(node);
            link(&list_);
        } else if (type == VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO) {
            stencil_ = *reinterpret_cast<const VkImageStencilUsageCreateInfo *>(node);
            link(&stencil_);
        } else if (type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT) {
            view_ = *reinterpret_cast<const VkPhysicalDeviceImageViewImageFormatInfoEXT *>(node);
            link(&view_);
        } else {
            return refusal();
        }
    }
    return Outcome::translated;
}

const VkPhysicalDeviceImageFormatInfo2 *ImageFormatChain::forwarded() const
{
    AV_SCOPE("ImageFormatChain::forwarded");
    return &info_;
}

VkFlags ImageFormatChain::asked_host_type() const
{
    AV_SCOPE("ImageFormatChain::asked_host_type");
    return static_cast<VkFlags>(external_.handleType);
}

void ImageFormatChain::report(VkImageFormatProperties2 &answer, const Win32HandleKind &kind) const
{
    AV_SCOPE("ImageFormatChain::report");
    uint32_t read = 0u;
    for (auto *node = static_cast<VkBaseOutStructure *>(answer.pNext); node != nullptr && read < longest_chain;
         node = node->pNext, read++) {
        if (static_cast<VkStructureType>(foreign_enum(node->sType)) != VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES)
            continue;
        auto &external = reinterpret_cast<VkExternalImageFormatProperties *>(node)->externalMemoryProperties;
        external.exportFromImportedHandleTypes = 0u;
        external.compatibleHandleTypes = kind.reported(external.compatibleHandleTypes, asked_host_type());
    }
}

void ImageFormatChain::link(void *node)
{
    AV_SCOPE("ImageFormatChain::link");
    auto *const added = static_cast<VkBaseOutStructure *>(node);
    added->pNext = nullptr;
    if (last_ == nullptr)
        info_.pNext = added;
    else
        last_->pNext = added;
    last_ = added;
}

void ImageFormatChain::clear()
{
    AV_SCOPE("ImageFormatChain::clear");
    info_ = VkPhysicalDeviceImageFormatInfo2{};
    external_ = VkPhysicalDeviceExternalImageFormatInfo{};
    list_ = VkImageFormatListCreateInfo{};
    stencil_ = VkImageStencilUsageCreateInfo{};
    view_ = VkPhysicalDeviceImageViewImageFormatInfoEXT{};
    last_ = nullptr;
}

ImageFormatChain::Outcome ImageFormatChain::refusal()
{
    AV_SCOPE("ImageFormatChain::refusal");
    clear();
    return Outcome::refused;
}

}
