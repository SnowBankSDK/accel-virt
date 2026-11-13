#include "executor/egl_entry_gate.hpp"
#include "executor/host_lifecycle_gate.hpp"
#include "executor/host_lifecycle_gates.hpp"
#include "harness/executor/fake_vulkan_driver.hpp"
#include "harness/executor/generous_budget.hpp"
#include "harness/executor/vulkan_wire_rig.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <chrono>
#include <pthread.h>
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

namespace {

struct EglRace {
    av::EglEntryGate  egl;
    std::atomic<bool> destroying{false};
    std::atomic<bool> try_entry{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> overlapped{false};
    std::atomic<std::uint32_t> inside_at_destroy{0u};
};

EglRace egl_race;

void destroy_while_an_entry_tries()
{
    egl_race.inside_at_destroy = egl_race.egl.inside();
    egl_race.destroying = true;
    egl_race.try_entry = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    if (egl_race.entered.load()) egl_race.overlapped = true;
    egl_race.destroying = false;
}

}

TEST(vulkan_device_destroy, on_a_driver_that_needs_it_a_device_destroy_and_an_egl_entry_of_any_driver_never_run_together_whichever_came_first)
{ static av::HostLifecycleGates gates(egl_race.egl);
  const std::uint8_t vendor_gpu[16] = {7};
  av::HostLifecycleGate *const gate =
      gates.add(vendor_gpu, av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "1"));
  CHECK(gate != nullptr);
  { Rig rig;
    av::fake_vulkan.destroying_device = destroy_while_an_entry_tries;
    rig.backend().gate_lifecycle(gate);
    std::thread entry([] {
        while (!egl_race.try_entry.load()) std::this_thread::yield();
        const av::EglEntry inside(egl_race.egl);
        if (egl_race.destroying.load()) egl_race.overlapped = true;
        egl_race.entered = true;
    });
    CHECK(rig.destroy_device());
    entry.join();
    CHECK_EQ(av::fake_vulkan.device_destroys, 1u);
    CHECK(egl_race.entered.load());
    CHECK(!egl_race.overlapped.load());
    CHECK_EQ(egl_race.egl.held_back(), 1u); }
  { Rig rig;
    av::fake_vulkan.destroying_device = destroy_while_an_entry_tries;
    rig.backend().gate_lifecycle(gate);
    egl_race.entered = false;
    egl_race.try_entry = false;
    std::atomic<bool> inside{false}, leave{false};
    std::thread       entry([&] {
        const av::EglEntry held(egl_race.egl);
        inside = true;
        while (!leave.load()) std::this_thread::yield();
    });
    while (!inside.load()) std::this_thread::yield();
    bool        answered = false;
    std::thread destroyer([&] { answered = rig.destroy_device(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK_EQ(av::fake_vulkan.device_destroys, 0u);
    leave = true;
    entry.join();
    destroyer.join();
    CHECK(answered);
    CHECK_EQ(av::fake_vulkan.device_destroys, 1u);
    CHECK_EQ(egl_race.inside_at_destroy.load(), 0u);
    CHECK(!egl_race.overlapped.load()); } }

namespace {

struct ExitRace {
    av::EglEntryGate  egl;
    pthread_key_t     thread_state;
    std::atomic<bool> destroying{false};
    std::atomic<bool> in_destructor{false};
    std::atomic<bool> overlapped{false};
    std::atomic<bool> exit_now{false};
    std::atomic<std::uint32_t> destructors{0u};
};

ExitRace exit_race;

void fake_egl_thread_state_destructor(void *)
{
    exit_race.in_destructor = true;
    if (exit_race.destroying.load()) exit_race.overlapped = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    if (exit_race.destroying.load()) exit_race.overlapped = true;
    exit_race.destructors++;
    exit_race.in_destructor = false;
}

void fake_egl_call()
{
    const av::EglEntry entered(exit_race.egl);
    pthread_setspecific(exit_race.thread_state, &exit_race);
}

void destroy_while_a_thread_exits()
{
    exit_race.destroying = true;
    if (exit_race.in_destructor.load()) exit_race.overlapped = true;
    exit_race.exit_now = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    if (exit_race.in_destructor.load()) exit_race.overlapped = true;
    exit_race.destroying = false;
}

void *egl_thread(void *)
{
    fake_egl_call();
    while (!exit_race.exit_now.load()) std::this_thread::yield();
    return exit_race.egl.retire();
}

}

TEST(vulkan_device_destroy, the_egl_librarys_thread_exit_destructor_of_a_thread_that_retired_never_runs_beside_a_device_destroy_whichever_came_first)
{ static av::HostLifecycleGates gates(exit_race.egl);
  const std::uint8_t vendor_gpu[16] = {9};
  av::HostLifecycleGate *const gate =
      gates.add(vendor_gpu, av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "1"));
  CHECK(gate != nullptr);
  CHECK_EQ(pthread_key_create(&exit_race.thread_state, fake_egl_thread_state_destructor), 0);
  { Rig rig;
    av::fake_vulkan.destroying_device = destroy_while_a_thread_exits;
    rig.backend().gate_lifecycle(gate);
    pthread_t thread;
    CHECK_EQ(pthread_create(&thread, nullptr, egl_thread, nullptr), 0);
    CHECK(rig.destroy_device());
    void *kept = nullptr;
    CHECK_EQ(pthread_join(thread, &kept), 0);
    CHECK(kept != nullptr);
    exit_race.egl.joined(kept);
    CHECK_EQ(av::fake_vulkan.device_destroys, 1u);
    CHECK_EQ(exit_race.destructors.load(), 1u);
    CHECK(!exit_race.overlapped.load()); }
  { Rig rig;
    av::fake_vulkan.destroying_device = destroy_while_a_thread_exits;
    rig.backend().gate_lifecycle(gate);
    exit_race.exit_now = true;
    pthread_t thread;
    CHECK_EQ(pthread_create(&thread, nullptr, egl_thread, nullptr), 0);
    while (!exit_race.in_destructor.load()) std::this_thread::yield();
    bool        answered = false;
    std::thread destroyer([&] { answered = rig.destroy_device(); });
    void *kept = nullptr;
    CHECK_EQ(pthread_join(thread, &kept), 0);
    CHECK_EQ(av::fake_vulkan.device_destroys, 0u);
    exit_race.egl.joined(kept);
    destroyer.join();
    CHECK(answered);
    CHECK_EQ(av::fake_vulkan.device_destroys, 1u);
    CHECK_EQ(exit_race.destructors.load(), 2u);
    CHECK(!exit_race.overlapped.load()); }
  CHECK_EQ(exit_race.egl.inside(), 0u); }
