#include "executor/vulkan/dma_buf_import_type.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstdint>

using av::DmaBufImportType;

namespace {

constexpr VkMemoryPropertyFlags local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
constexpr VkMemoryPropertyFlags visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
constexpr VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr VkMemoryPropertyFlags cached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
constexpr std::uint32_t         none = DmaBufImportType::no_type;

VkPhysicalDeviceMemoryProperties resizable_bar_discrete()
{ VkPhysicalDeviceMemoryProperties properties = {};
  const VkMemoryType types[] = {{0u, 1u},
                                {local, 0u},
                                {local, 0u},
                                {visible | coherent, 1u},
                                {visible | coherent | cached, 1u},
                                {local | visible | coherent, 0u}};
  properties.memoryTypeCount = 6u;
  for (std::uint32_t type = 0u; type < 6u; type++) properties.memoryTypes[type] = types[type];
  properties.memoryHeapCount = 2u;
  properties.memoryHeaps[0] = VkMemoryHeap{0x200000000ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
  properties.memoryHeaps[1] = VkMemoryHeap{0x2dd8b1000ull, 0u};
  return properties; }

}

TEST(dma_buf_import_type, system_memory_is_imported_as_a_type_the_gpu_reads_coherently_with_the_cpu_cache)
{ const VkPhysicalDeviceMemoryProperties memory = resizable_bar_discrete();
  const DmaBufImportType                 imports(memory);
  CHECK_EQ(imports.choose(0x11u), 4u);
  CHECK_EQ(imports.choose(0x19u), 4u);
  CHECK_EQ(imports.choose(0x09u), 3u); }

TEST(dma_buf_import_type, device_memory_is_imported_as_device_local)
{ const VkPhysicalDeviceMemoryProperties memory = resizable_bar_discrete();
  const DmaBufImportType                 imports(memory);
  CHECK_EQ(imports.choose(0x03u), 1u);
  CHECK_EQ(imports.choose(0x30u), 5u);
  CHECK_EQ(imports.choose(0x24u), 2u); }

TEST(dma_buf_import_type, a_memory_without_host_access_or_locality_is_taken_when_nothing_else_is_allowed)
{ const VkPhysicalDeviceMemoryProperties memory = resizable_bar_discrete();
  const DmaBufImportType                 imports(memory);
  CHECK_EQ(imports.choose(0x01u), 0u); }

TEST(dma_buf_import_type, nothing_is_chosen_when_no_type_is_allowed_or_the_allowed_ones_do_not_exist)
{ const VkPhysicalDeviceMemoryProperties memory = resizable_bar_discrete();
  const DmaBufImportType                 imports(memory);
  CHECK_EQ(imports.choose(0u), none);
  CHECK_EQ(imports.choose(0xc0u), none);
  CHECK_EQ(imports.choose(0x80000000u), none); }

TEST(dma_buf_import_type, it_follows_the_device_it_was_given_once_that_device_is_learned)
{ VkPhysicalDeviceMemoryProperties memory = {};
  const DmaBufImportType           imports(memory);
  CHECK_EQ(imports.choose(0x11u), none);
  memory = resizable_bar_discrete();
  CHECK_EQ(imports.choose(0x11u), 4u); }

TEST(dma_buf_import_type, memory_from_another_gpu_is_never_offered_a_type_with_host_access)
{ const VkPhysicalDeviceMemoryProperties memory = resizable_bar_discrete();
  const DmaBufImportType                 imports(memory);
  CHECK_EQ(imports.from_elsewhere(0x11u), 0x01u);
  CHECK_EQ(imports.choose(imports.from_elsewhere(0x11u)), 0u);
  CHECK_EQ(imports.from_elsewhere(0x3bu), 0x03u);
  CHECK_EQ(imports.from_elsewhere(0x3fu), 0x07u); }

TEST(dma_buf_import_type, memory_from_another_gpu_keeps_host_access_types_when_it_has_no_others)
{ const VkPhysicalDeviceMemoryProperties memory = resizable_bar_discrete();
  const DmaBufImportType                 imports(memory);
  CHECK_EQ(imports.from_elsewhere(0x18u), 0x18u);
  CHECK_EQ(imports.from_elsewhere(0u), 0u); }
