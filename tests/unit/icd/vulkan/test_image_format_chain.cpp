#include "harness/framework/test_registry.hpp"
#include "icd/vulkan/image_format_chain.hpp"
#include "icd/vulkan/win32_handle_kind.hpp"

using av::icd::ImageFormatChain;
using av::icd::Win32HandleKind;

namespace {
constexpr VkFlags dma_buf = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

VkPhysicalDeviceImageFormatInfo2 asked(const void *chain)
{
    VkPhysicalDeviceImageFormatInfo2 info = {};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.pNext = chain;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return info;
}

VkPhysicalDeviceExternalImageFormatInfo external(VkExternalMemoryHandleTypeFlagBits type, const void *next)
{
    VkPhysicalDeviceExternalImageFormatInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    info.pNext = next;
    info.handleType = type;
    return info;
}
}

TEST(image_format_chain, a_query_that_names_no_external_handle_goes_to_the_host_as_the_app_wrote_it)
{ Win32HandleKind kind(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
  ImageFormatChain chain;
  const VkPhysicalDeviceImageFormatInfo2 plain = asked(nullptr);
  CHECK(chain.take(plain, kind, dma_buf) == ImageFormatChain::Outcome::untouched);
  const VkPhysicalDeviceExternalImageFormatInfo none = external(static_cast<VkExternalMemoryHandleTypeFlagBits>(0), nullptr);
  const VkPhysicalDeviceImageFormatInfo2 empty = asked(&none);
  CHECK(chain.take(empty, kind, dma_buf) == ImageFormatChain::Outcome::untouched); }

TEST(image_format_chain, an_opaque_win32_image_is_asked_of_the_host_as_the_type_it_shares_with_and_other_nodes_are_kept)
{ Win32HandleKind kind(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
  const VkFormat views[2] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB};
  VkImageFormatListCreateInfo list = {};
  list.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
  list.viewFormatCount = 2u;
  list.pViewFormats = views;
  const VkPhysicalDeviceExternalImageFormatInfo win32 = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, &list);
  const VkPhysicalDeviceImageFormatInfo2 info = asked(&win32);
  ImageFormatChain chain;
  CHECK(chain.take(info, kind, dma_buf) == ImageFormatChain::Outcome::translated);
  CHECK_EQ(chain.asked_host_type(), dma_buf);
  const VkPhysicalDeviceImageFormatInfo2 *sent = chain.forwarded();
  CHECK_EQ(static_cast<uint32_t>(sent->format), static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM));
  CHECK_EQ(sent->usage, info.usage);
  const auto *first = static_cast<const VkPhysicalDeviceExternalImageFormatInfo *>(sent->pNext);
  CHECK(first != nullptr && first != &win32);
  CHECK_EQ(static_cast<uint32_t>(first->sType), static_cast<uint32_t>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO));
  CHECK_EQ(static_cast<VkFlags>(first->handleType), dma_buf);
  const auto *second = static_cast<const VkImageFormatListCreateInfo *>(first->pNext);
  CHECK(second != nullptr && second != &list && second->viewFormatCount == 2u && second->pViewFormats == views);
  CHECK(second->pNext == nullptr);
  CHECK_EQ(static_cast<VkFlags>(win32.handleType), static_cast<VkFlags>(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT)); }

TEST(image_format_chain, a_handle_type_the_guest_does_not_share_or_a_chain_it_cannot_carry_is_not_supported)
{ Win32HandleKind kind(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
  ImageFormatChain chain;
  const VkPhysicalDeviceExternalImageFormatInfo texture = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, nullptr);
  const VkPhysicalDeviceImageFormatInfo2 d3d = asked(&texture);
  CHECK(chain.take(d3d, kind, dma_buf) == ImageFormatChain::Outcome::refused);
  const VkPhysicalDeviceExternalImageFormatInfo fd = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, nullptr);
  const VkPhysicalDeviceImageFormatInfo2 linux_type = asked(&fd);
  CHECK(chain.take(linux_type, kind, dma_buf) == ImageFormatChain::Outcome::refused);
  VkBaseInStructure unknown = {};
  unknown.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
  const VkPhysicalDeviceExternalImageFormatInfo with_unknown = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, &unknown);
  const VkPhysicalDeviceImageFormatInfo2 odd = asked(&with_unknown);
  CHECK(chain.take(odd, kind, dma_buf) == ImageFormatChain::Outcome::refused);
  const VkPhysicalDeviceExternalImageFormatInfo again = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, nullptr);
  const VkPhysicalDeviceExternalImageFormatInfo twice = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, &again);
  const VkPhysicalDeviceImageFormatInfo2 doubled = asked(&twice);
  CHECK(chain.take(doubled, kind, dma_buf) == ImageFormatChain::Outcome::refused); }

TEST(image_format_chain, the_host_s_answer_offers_the_opaque_win32_type_when_the_host_shares_what_it_was_asked_about)
{ Win32HandleKind kind(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
  const VkPhysicalDeviceExternalImageFormatInfo win32 = external(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT, nullptr);
  const VkPhysicalDeviceImageFormatInfo2 info = asked(&win32);
  ImageFormatChain chain;
  CHECK(chain.take(info, kind, dma_buf) == ImageFormatChain::Outcome::translated);
  VkExternalImageFormatProperties external_answer = {};
  external_answer.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
  external_answer.externalMemoryProperties.externalMemoryFeatures =
      VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
  external_answer.externalMemoryProperties.compatibleHandleTypes = dma_buf;
  external_answer.externalMemoryProperties.exportFromImportedHandleTypes = dma_buf;
  VkImageFormatProperties2 answer = {};
  answer.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
  answer.pNext = &external_answer;
  chain.report(answer, kind);
  CHECK_EQ(external_answer.externalMemoryProperties.compatibleHandleTypes,
           static_cast<VkFlags>(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT));
  CHECK_EQ(external_answer.externalMemoryProperties.exportFromImportedHandleTypes, 0u);
  CHECK_EQ(external_answer.externalMemoryProperties.externalMemoryFeatures,
           static_cast<VkFlags>(VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT));
  external_answer.externalMemoryProperties.compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  chain.report(answer, kind);
  CHECK_EQ(external_answer.externalMemoryProperties.compatibleHandleTypes, 0u); }
