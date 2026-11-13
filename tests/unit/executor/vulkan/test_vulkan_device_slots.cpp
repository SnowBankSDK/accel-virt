#include "core/log/driver_log.hpp"
#include "executor/host_device_slots.hpp"
#include "executor/host_driver.hpp"
#include "executor/host_lifecycle_gate.hpp"
#include "executor/host_lifecycle_gates.hpp"
#include "executor/vulkan/vulkan_backend.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/icd/recording_log.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <thread>

using av::HostDeviceSlots;
using av::HostLifecycleGate;
using av::VulkanBackend;
using HandleKind = av::vkproto::HandleKind;

namespace {

constexpr std::uint32_t most_families = 4u;
constexpr std::uint32_t pooled_capacity = 239u;
constexpr std::uint32_t plain_device = 4u;
constexpr std::uint32_t every_family_device = 6u;

struct FakeDriver {
    std::uint32_t families = 3u;
    VkQueueFlags  flags[most_families] = {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
                                          VK_QUEUE_TRANSFER_BIT, VK_QUEUE_COMPUTE_BIT, 0u};
    std::uint32_t queues[most_families] = {16u, 2u, 8u, 0u};
    std::uint32_t family_queries = 0u;
    std::uint32_t device_creates = 0u;
    std::uint32_t device_destroys = 0u;
    VkResult      create_result = VK_SUCCESS;
};

FakeDriver driver;

template <typename H>
H as_handle(std::uint64_t raw)
{ H handle = VK_NULL_HANDLE;
  std::memcpy(&handle, &raw, sizeof handle);
  return handle; }

constexpr std::uint64_t instance = 0x100u;
constexpr std::uint64_t physical = 0x200u;
constexpr std::uint64_t first_device = 0x3000u;

struct Shape {
    explicit Shape(std::uint32_t families_named, std::uint32_t first_family_queues = 1u) : queues{}, create{}
    {
        for (std::uint32_t family = 0u; family < families_named; family++) {
            queues[family].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queues[family].queueFamilyIndex = family;
            queues[family].queueCount = family == 0u ? first_family_queues : 1u;
            queues[family].pQueuePriorities = priorities;
        }
        create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create.queueCreateInfoCount = families_named;
        create.pQueueCreateInfos = queues;
    }

    float                   priorities[16] = {};
    VkDeviceQueueCreateInfo queues[most_families];
    VkDeviceCreateInfo      create;
};

class Rig {
public:
    explicit Rig(HostLifecycleGate *gate) : sink_(), lines_(sink_, "vulkan: "), backend_(lines_)
    {
        driver = FakeDriver{};
        backend_.made(HandleKind::instance, instance);
        backend_.made_through(instance);
        backend_.made(HandleKind::physical_device, physical);
        backend_.made_through(0u);
        backend_.gate_lifecycle(gate);
    }
    ~Rig() { backend_.shut_down(); }

    VkResult create(const Shape &shape, VkDevice &made)
    { made = VK_NULL_HANDLE;
      return backend_.on_vkCreateDevice(as_handle<VkPhysicalDevice>(physical), &shape.create, nullptr, &made); }

    void destroy(VkDevice device) { backend_.on_vkDestroyDevice(device, nullptr); }

private:
    av::RecordingLog sink_;
    av::DriverLog    lines_;
    VulkanBackend    backend_;
};

struct PooledGate {
    explicit PooledGate(std::uint64_t wait_ns = HostLifecycleGate::default_wait_ns) : gate(wait_ns)
    { gate.driven_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY); }

    HostLifecycleGate gate;
};

}

extern "C" {

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t *count,
                                                                    VkQueueFamilyProperties *properties)
{ driver.family_queries++;
  if (properties == nullptr) {
      *count = driver.families;
      return;
  }
  if (*count > driver.families) *count = driver.families;
  for (uint32_t family = 0u; family < *count; family++) {
      properties[family] = {};
      properties[family].queueFlags = driver.flags[family];
      properties[family].queueCount = driver.queues[family];
  } }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures *features)
{ *features = {};
  features->robustBufferAccess = VK_TRUE; }

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char *, uint32_t *count,
                                                                    VkExtensionProperties *)
{ *count = 0u;
  return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *,
                                              VkDevice *out)
{ driver.device_creates++;
  if (driver.create_result != VK_SUCCESS) return driver.create_result;
  *out = as_handle<VkDevice>(first_device + 0x10u * driver.device_creates);
  return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice, const VkAllocationCallbacks *)
{ driver.device_destroys++; }

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance, const VkAllocationCallbacks *)
{}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice)
{ return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t)
{ return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *properties)
{ *properties = VkPhysicalDeviceMemoryProperties{}; }

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char *name)
{ if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0)
      return reinterpret_cast<PFN_vkVoidFunction>(&vkGetPhysicalDeviceMemoryProperties);
  return nullptr; }

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char *)
{ return nullptr; }

}

TEST(vulkan_device_slots, a_device_costs_its_base_two_per_graphics_queue_and_one_per_other_queue_on_the_pooled_driver)
{ HostDeviceSlots slots;
  slots.driven_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY);
  const FakeDriver host;
  VkQueueFamilyProperties families[most_families] = {};
  for (std::uint32_t family = 0u; family < host.families; family++) families[family].queueFlags = host.flags[family];
  CHECK(slots.bounded());
  CHECK_EQ(slots.capacity(), pooled_capacity);
  CHECK_EQ(slots.device_slots(families, host.families, Shape(1u).create), plain_device);
  CHECK_EQ(slots.device_slots(families, host.families, Shape(3u).create), every_family_device);
  CHECK_EQ(slots.device_slots(families, host.families, Shape(1u, 16u).create), 34u);
  CHECK_EQ(slots.device_slots(families, 0u, Shape(3u).create), 8u);
  CHECK(slots.invariants_hold()); }

TEST(vulkan_device_slots, a_device_the_pooled_driver_has_no_room_for_is_refused_before_the_driver_is_entered_and_counted)
{ PooledGate pooled;
  Rig        rig(&pooled.gate);
  CHECK(pooled.gate.devices().take(pooled_capacity - every_family_device + 1u));
  VkDevice made = VK_NULL_HANDLE;
  CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made)), static_cast<int>(VK_ERROR_TOO_MANY_OBJECTS));
  CHECK(made == VK_NULL_HANDLE);
  CHECK_EQ(driver.device_creates, 0u);
  CHECK_EQ(pooled.gate.devices().refusals(), 1u);
  CHECK_EQ(pooled.gate.devices().held(), pooled_capacity - every_family_device + 1u);
  pooled.gate.devices().give(1u);
  CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made)), static_cast<int>(VK_SUCCESS));
  CHECK_EQ(driver.device_creates, 1u);
  CHECK_EQ(pooled.gate.devices().held(), pooled_capacity);
  rig.destroy(made); }

TEST(vulkan_device_slots, destroying_a_device_returns_its_room)
{ PooledGate pooled;
  Rig        rig(&pooled.gate);
  VkDevice   made = VK_NULL_HANDLE;
  CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made)), static_cast<int>(VK_SUCCESS));
  CHECK_EQ(pooled.gate.devices().held(), every_family_device);
  rig.destroy(made);
  CHECK_EQ(driver.device_destroys, 1u);
  CHECK_EQ(pooled.gate.devices().held(), 0u); }

TEST(vulkan_device_slots, a_create_the_driver_fails_returns_its_room)
{ PooledGate pooled;
  Rig        rig(&pooled.gate);
  driver.create_result = VK_ERROR_INITIALIZATION_FAILED;
  VkDevice made = VK_NULL_HANDLE;
  CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made)), static_cast<int>(VK_ERROR_INITIALIZATION_FAILED));
  CHECK(driver.device_creates >= 1u);
  CHECK_EQ(pooled.gate.devices().held(), 0u);
  CHECK_EQ(pooled.gate.devices().refusals(), 0u); }

TEST(vulkan_device_slots, devices_are_created_up_to_the_bound_and_the_driver_never_sees_one_past_it)
{ PooledGate pooled;
  Rig        rig(&pooled.gate);
  constexpr std::uint32_t fitting = pooled_capacity / plain_device;
  VkDevice made[fitting + 1u] = {};
  for (std::uint32_t at = 0u; at < fitting; at++)
      CHECK_EQ(static_cast<int>(rig.create(Shape(1u), made[at])), static_cast<int>(VK_SUCCESS));
  CHECK_EQ(driver.device_creates, fitting);
  CHECK_EQ(static_cast<int>(rig.create(Shape(1u), made[fitting])), static_cast<int>(VK_ERROR_TOO_MANY_OBJECTS));
  CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made[fitting])), static_cast<int>(VK_ERROR_TOO_MANY_OBJECTS));
  CHECK_EQ(driver.device_creates, fitting);
  CHECK_EQ(pooled.gate.devices().refusals(), 2u);
  CHECK(pooled.gate.devices().invariants_hold());
  for (std::uint32_t at = 0u; at < fitting; at++) rig.destroy(made[at]);
  CHECK_EQ(driver.device_destroys, fitting);
  CHECK_EQ(pooled.gate.devices().held(), 0u); }

TEST(vulkan_device_slots, a_device_leaked_because_its_gate_stayed_held_keeps_its_room)
{ constexpr std::uint64_t wait_ns = 50000000u;
  PooledGate pooled(wait_ns);
  Rig        rig(&pooled.gate);
  VkDevice   made = VK_NULL_HANDLE;
  CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made)), static_cast<int>(VK_SUCCESS));
  CHECK(pooled.gate.enter());
  std::thread destroyer([&] { rig.destroy(made); });
  destroyer.join();
  pooled.gate.leave();
  CHECK_EQ(driver.device_destroys, 0u);
  CHECK_EQ(pooled.gate.devices().held(), every_family_device); }

TEST(vulkan_device_slots, the_embedder_table_gl_contexts_and_the_backend_draw_on_one_ledger_of_their_driver)
{ const std::uint8_t     uuid[16] = {7u, 1u};
  av::HostLifecycleGates gates;
  HostLifecycleGate *const gate = gates.add(uuid, av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "pooled", "1"));
  CHECK(gate != nullptr);
  const AvLifecycleGates &abi = gates.abi();
  void *const             named = abi.gate_of(abi.self, uuid);
  CHECK(named == gate);
  Rig           rig(gate);
  const Shape   embedded(3u);
  std::uint32_t embedder_slots[64] = {};
  std::uint32_t embedder_devices = 0u;
  while (embedder_devices < 64u &&
         abi.admit_device(named, as_handle<VkPhysicalDevice>(physical), &embedded.create,
                          &embedder_slots[embedder_devices]) != 0)
      embedder_devices++;
  CHECK_EQ(embedder_devices, pooled_capacity / every_family_device);
  CHECK_EQ(embedder_slots[0], every_family_device);
  std::uint32_t contexts = 0u;
  while (gate->devices().take(HostDeviceSlots::context_slots)) contexts++;
  CHECK_EQ(contexts, pooled_capacity % every_family_device);
  VkDevice made = VK_NULL_HANDLE;
  CHECK_EQ(static_cast<int>(rig.create(Shape(1u), made)), static_cast<int>(VK_ERROR_TOO_MANY_OBJECTS));
  CHECK_EQ(driver.device_creates, 0u);
  abi.return_device(named, embedder_slots[0]);
  CHECK_EQ(static_cast<int>(rig.create(Shape(1u), made)), static_cast<int>(VK_SUCCESS));
  CHECK_EQ(gate->devices().held(), pooled_capacity - every_family_device + plain_device);
  rig.destroy(made); }

TEST(vulkan_device_slots, a_driver_without_the_pool_is_not_bounded_and_pays_nothing_for_it)
{ std::uint32_t ungated_queries = 0u;
  { Rig      rig(nullptr);
    VkDevice made = VK_NULL_HANDLE;
    CHECK_EQ(static_cast<int>(rig.create(Shape(3u), made)), static_cast<int>(VK_SUCCESS));
    ungated_queries = driver.family_queries;
    rig.destroy(made); }
  HostLifecycleGate gate;
  gate.driven_by(VK_DRIVER_ID_MESA_RADV);
  Rig rig(&gate);
  constexpr std::uint32_t many = 96u;
  VkDevice made[many] = {};
  for (std::uint32_t at = 0u; at < many; at++)
      CHECK_EQ(static_cast<int>(rig.create(Shape(3u, 16u), made[at])), static_cast<int>(VK_SUCCESS));
  CHECK_EQ(driver.device_creates, many);
  CHECK_EQ(driver.family_queries, many * ungated_queries);
  CHECK(!gate.devices().bounded());
  CHECK_EQ(gate.devices().held(), 0u);
  CHECK_EQ(gate.devices().refusals(), 0u);
  for (std::uint32_t at = 0u; at < many; at++) rig.destroy(made[at]);
  CHECK_EQ(driver.device_destroys, many); }
