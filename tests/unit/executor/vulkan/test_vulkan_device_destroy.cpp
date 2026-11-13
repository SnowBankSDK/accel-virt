#include "executor/host_lifecycle_gate.hpp"
#include "harness/executor/fake_vulkan_driver.hpp"
#include "harness/executor/generous_budget.hpp"
#include "harness/executor/vulkan_wire_rig.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

namespace {

struct Rig : av::VulkanWireRig {
    Rig()
    {
        av::fake_vulkan = av::FakeVulkanDriver{};
        av::fake_vulkan.families = 1u;
        av::fake_vulkan.family_flags[0] = VK_QUEUE_GRAPHICS_BIT;
        av::fake_vulkan.fence_status = VK_NOT_READY;
        recorder = command_buffer(pool(0u));
    }

    std::uint64_t recorder = 0u;
};

struct DriverCalls {
    std::atomic<std::uint32_t> inside{0u};
    std::atomic<std::uint32_t> most{0u};
    std::atomic<std::uint32_t> calls{0u};

    void call()
    {
        const std::uint32_t now = ++inside;
        std::uint32_t       seen = most.load();
        while (now > seen && !most.compare_exchange_weak(seen, now)) {}
        calls++;
        for (std::uint32_t spin = 0u; spin < 4u; spin++) std::this_thread::yield();
        inside--;
    }
};

DriverCalls driver_calls;

void fake_destroy_device() { driver_calls.call(); }

void fake_egl_destroy_context(av::HostLifecycleGate &gate)
{
    const av::HostLifecycleHold hold(&gate);
    if (hold.held()) driver_calls.call();
}

void fake_cl_create_context(av::HostLifecycleGate &gate)
{
    const av::HostLifecycleHold hold(&gate);
    if (hold.held()) driver_calls.call();
}

}

TEST(vulkan_device_destroy, an_idle_device_is_destroyed_in_the_driver)
{ Rig rig;
  CHECK(rig.destroy_device());
  CHECK_EQ(av::fake_vulkan.device_destroys, 1u); }

TEST(vulkan_device_destroy, a_device_whose_work_finished_is_destroyed_in_the_driver)
{ Rig rig;
  CHECK(rig.submit(rig.queue, rig.recorder, rig.fence()));
  av::fake_vulkan.wait_result = VK_SUCCESS;
  CHECK(rig.destroy_device());
  CHECK_EQ(av::fake_vulkan.device_destroys, 1u); }

TEST(vulkan_device_destroy, a_device_whose_work_does_not_finish_is_leaked_not_destroyed)
{ Rig rig;
  CHECK(rig.submit(rig.queue, rig.recorder, rig.fence()));
  av::fake_vulkan.wait_result = VK_TIMEOUT;
  CHECK(rig.destroy_device());
  CHECK_EQ(av::fake_vulkan.device_destroys, 0u); }

TEST(vulkan_device_destroy, a_device_that_reports_loss_while_waited_on_is_leaked_not_destroyed)
{ Rig rig;
  CHECK(rig.submit(rig.queue, rig.recorder, rig.fence()));
  av::fake_vulkan.wait_result = VK_ERROR_DEVICE_LOST;
  CHECK(rig.destroy_device());
  CHECK_EQ(av::fake_vulkan.device_destroys, 0u);
  CHECK(rig.backend().lost()); }

TEST(vulkan_device_destroy, a_lost_device_is_never_counted_as_drained_so_its_deferred_destroys_never_run)
{ Rig rig;
  CHECK(rig.submit(rig.queue, rig.recorder, rig.fence()));
  av::fake_vulkan.fence_status = VK_ERROR_DEVICE_LOST;
  av::fake_vulkan.wait_result = VK_ERROR_DEVICE_LOST;
  CHECK(!rig.backend().lost());
  av::GenerousBudget budget;
  CHECK(!rig.backend().drain(budget.budget()));
  CHECK(rig.backend().lost()); }

TEST(vulkan_device_destroy, a_device_destroy_never_runs_in_the_driver_beside_a_gl_context_destroy_or_a_cl_context_create_of_its_gpu)
{ constexpr std::uint32_t rounds = 300u;
  av::HostLifecycleGate gate;
  driver_calls.most = 0u;
  driver_calls.calls = 0u;
  std::atomic<bool> started{false};
  std::thread       gl([&] {
      while (!started.load()) std::this_thread::yield();
      for (std::uint32_t round = 0u; round < rounds; round++) fake_egl_destroy_context(gate);
  });
  std::thread cl([&] {
      while (!started.load()) std::this_thread::yield();
      for (std::uint32_t round = 0u; round < rounds; round++) fake_cl_create_context(gate);
  });
  std::uint32_t destroyed = 0u;
  for (std::uint32_t round = 0u; round < rounds; round++) {
      Rig rig;
      av::fake_vulkan.destroying_device = fake_destroy_device;
      rig.backend().gate_lifecycle(&gate);
      started = true;
      CHECK(rig.destroy_device());
      destroyed += av::fake_vulkan.device_destroys;
  }
  gl.join();
  cl.join();
  CHECK_EQ(destroyed, rounds);
  CHECK_EQ(driver_calls.calls.load(), 3u * rounds);
  CHECK_EQ(driver_calls.most.load(), 1u);
  CHECK_EQ(gate.entries(), std::uint64_t{3u} * rounds);
  CHECK_EQ(gate.refusals(), 0u); }

TEST(vulkan_device_destroy, a_device_whose_gpus_lifecycle_gate_stays_held_is_leaked_and_reported_not_destroyed)
{ constexpr std::uint64_t wait_ns = 50000000u;
  av::HostLifecycleGate   gate(wait_ns);
  Rig                     rig;
  rig.backend().gate_lifecycle(&gate);
  CHECK(gate.enter());
  bool        answered = false;
  std::thread destroyer([&] { answered = rig.destroy_device(); });
  destroyer.join();
  gate.leave();
  CHECK(answered);
  CHECK_EQ(av::fake_vulkan.device_destroys, 0u);
  CHECK_EQ(gate.refusals(), 1u);
  CHECK_EQ(rig.sink().holding("leaked instead of destroyed"), 1u); }
