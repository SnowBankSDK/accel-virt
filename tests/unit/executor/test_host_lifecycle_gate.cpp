#include "executor/host_lifecycle_gate.hpp"
#include "executor/host_lifecycle_gates.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using av::HostLifecycleGate;
using av::HostLifecycleGates;
using av::HostLifecycleHold;
using av::HostLifecycleShare;

namespace {

constexpr std::uint64_t short_wait_ns = 50000000u;

}

TEST(host_lifecycle_gate, two_holders_are_never_inside_at_once)
{ HostLifecycleGate gate;
  std::atomic<std::uint32_t> inside{0u};
  std::atomic<std::uint32_t> most{0u};
  std::atomic<std::uint32_t> refused{0u};
  constexpr std::uint32_t threads = 4u, rounds = 200u;
  std::thread workers[threads];
  for (std::thread &worker : workers)
      worker = std::thread([&] {
          for (std::uint32_t round = 0u; round < rounds; round++) {
              const HostLifecycleHold hold(&gate);
              if (!hold.held()) {
                  refused++;
                  continue;
              }
              const std::uint32_t now = ++inside;
              std::uint32_t       seen = most.load();
              while (now > seen && !most.compare_exchange_weak(seen, now)) {}
              std::this_thread::yield();
              inside--;
          }
      });
  for (std::thread &worker : workers) worker.join();
  CHECK_EQ(most.load(), 1u);
  CHECK_EQ(refused.load(), 0u);
  CHECK_EQ(gate.entries(), std::uint64_t{threads} * rounds);
  CHECK_EQ(gate.refusals(), 0u); }

TEST(host_lifecycle_gate, a_holder_that_does_not_come_out_in_time_refuses_the_next_and_counts_it)
{ HostLifecycleGate gate(short_wait_ns);
  CHECK(gate.enter());
  bool                                   held = true;
  std::chrono::steady_clock::duration    waited{};
  std::thread                            other([&] {
      const auto started = std::chrono::steady_clock::now();
      const HostLifecycleHold hold(&gate);
      held = hold.held();
      waited = std::chrono::steady_clock::now() - started;
  });
  other.join();
  CHECK(!held);
  CHECK(waited >= std::chrono::nanoseconds(short_wait_ns));
  CHECK_EQ(gate.refusals(), 1u);
  gate.leave();
  { const HostLifecycleHold hold(&gate);
    CHECK(hold.held()); }
  CHECK(gate.enter());
  gate.leave();
  CHECK_EQ(gate.entries(), 3u);
  CHECK_EQ(gate.refusals(), 1u); }

TEST(host_lifecycle_gate, a_hold_with_no_gate_is_held_and_takes_nothing)
{ const HostLifecycleHold hold(nullptr);
  CHECK(hold.held()); }

TEST(host_lifecycle_gate, only_the_driver_whose_ray_tracing_compiles_deadlock_device_destruction_shares_compiles)
{ HostLifecycleGate gate;
  CHECK(!gate.shares_compiles());
  for (VkDriverId driver : {VK_DRIVER_ID_MESA_RADV, VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, VK_DRIVER_ID_MESA_NVK,
                            VK_DRIVER_ID_AMD_PROPRIETARY, static_cast<VkDriverId>(0)}) {
      gate.driven_by(driver);
      CHECK(!gate.shares_compiles());
  }
  CHECK(gate.enter());
  { const HostLifecycleShare share(&gate);
    CHECK(share.held()); }
  gate.leave();
  CHECK_EQ(gate.shares(), 0u);
  gate.driven_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY);
  CHECK(gate.shares_compiles()); }

TEST(host_lifecycle_gate, compiles_run_side_by_side_and_a_lifecycle_call_waits_for_them)
{ HostLifecycleGate gate;
  gate.driven_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY);
  CHECK(gate.share());
  CHECK(gate.share());
  gate.unshare();
  std::atomic<bool> entered{false};
  std::thread       destroyer([&] {
      const HostLifecycleHold hold(&gate);
      entered = hold.held();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!entered.load());
  gate.unshare();
  destroyer.join();
  CHECK(entered.load());
  CHECK_EQ(gate.shares(), 2u);
  CHECK_EQ(gate.entries(), 1u);
  CHECK_EQ(gate.refusals(), 0u); }

TEST(host_lifecycle_gate, a_compile_waits_for_a_lifecycle_call_inside_and_is_refused_when_it_stays_inside)
{ HostLifecycleGate gate(short_wait_ns);
  gate.driven_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY);
  CHECK(gate.enter());
  bool        shared = true;
  std::thread compiler([&] {
      const HostLifecycleShare share(&gate);
      shared = share.held();
  });
  compiler.join();
  CHECK(!shared);
  CHECK_EQ(gate.refusals(), 1u);
  gate.leave();
  { const HostLifecycleShare share(&gate);
    CHECK(share.held()); }
  CHECK_EQ(gate.shares(), 1u); }

TEST(host_lifecycle_gate, a_lifecycle_call_that_cannot_enter_behind_a_compile_in_time_is_refused_and_counted)
{ HostLifecycleGate gate(short_wait_ns);
  gate.driven_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY);
  CHECK(gate.share());
  bool        held = true;
  std::thread destroyer([&] {
      const HostLifecycleHold hold(&gate);
      held = hold.held();
  });
  destroyer.join();
  CHECK(!held);
  CHECK_EQ(gate.refusals(), 1u);
  gate.unshare(); }

TEST(host_lifecycle_gate, two_gpus_of_one_host_driver_share_a_gate_so_their_lifecycle_calls_never_overlap)
{ HostLifecycleGates gates;
  const std::uint8_t first[16] = {1}, second[16] = {2}, third[16] = {3};
  const av::HostDriver vendor(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "build 2");
  const av::HostDriver same_vendor(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "build 2");
  const av::HostDriver other_build(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "build 1");
  HostLifecycleGate *const one = gates.add(first, vendor);
  HostLifecycleGate *const two = gates.add(second, same_vendor);
  HostLifecycleGate *const three = gates.add(third, other_build);
  CHECK(one != nullptr);
  CHECK(two == one);
  CHECK(three != nullptr && three != one);
  CHECK(gates.of(first) == one);
  CHECK(gates.of(second) == one);
  CHECK(gates.of(third) == three);
  CHECK(gates.of(first)->enter());
  std::atomic<bool> inside{false};
  std::thread       other([&] {
      const HostLifecycleHold hold(gates.of(second));
      inside = hold.held();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  CHECK(!inside.load());
  {
      const HostLifecycleHold unrelated(gates.of(third));
      CHECK(unrelated.held());
  }
  gates.of(first)->leave();
  other.join();
  CHECK(inside.load());
  CHECK_EQ(one->entries(), 2u);
  CHECK_EQ(three->entries(), 1u); }

TEST(host_lifecycle_gate, the_gpus_of_two_host_drivers_have_a_gate_each_found_by_uuid_and_a_held_one_holds_no_other)
{ HostLifecycleGates gates;
  const std::uint8_t first[16] = {1}, second[16] = {2}, unknown[16] = {3};
  const av::HostDriver vendor(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "build 2");
  const av::HostDriver mesa(VK_DRIVER_ID_MESA_RADV, "open", "build 7");
  HostLifecycleGate *const one = gates.add(first, vendor);
  HostLifecycleGate *const two = gates.add(second, mesa);
  CHECK(one != nullptr);
  CHECK(two != nullptr);
  CHECK(one != two);
  CHECK(gates.add(first, vendor) == one);
  CHECK(gates.of(first) == one);
  CHECK(gates.of(second) == two);
  CHECK(gates.of(unknown) == nullptr);
  CHECK(gates.of(nullptr) == nullptr);
  CHECK(one->shares_compiles());
  CHECK(!two->shares_compiles());
  CHECK(one->enter());
  std::thread other([&] {
      const HostLifecycleHold hold(two);
      CHECK(hold.held());
  });
  other.join();
  one->leave();
  CHECK_EQ(two->entries(), 1u); }

TEST(host_lifecycle_gate, the_embedder_table_finds_the_gate_of_a_gpu_and_enters_shares_and_refuses_as_the_gate_does)
{ HostLifecycleGates gates;
  const std::uint8_t uuid[16] = {9}, unknown[16] = {8};
  HostLifecycleGate *const gate = gates.add(uuid, av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "build 2"));
  const AvLifecycleGates &table = gates.abi();
  CHECK_EQ(table.size, static_cast<std::uint32_t>(sizeof(AvLifecycleGates)));
  CHECK(table.gate_of(table.self, unknown) == nullptr);
  void *const found = table.gate_of(table.self, uuid);
  CHECK(found == gate);
  CHECK_EQ(table.share(found), 1);
  std::atomic<bool> destroyed{false};
  std::thread       destroyer([&] {
      CHECK_EQ(table.enter(found), 1);
      destroyed = true;
      table.leave(found);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!destroyed.load());
  table.unshare(found);
  destroyer.join();
  CHECK(destroyed.load());
  CHECK_EQ(gate->entries(), 1u);
  CHECK_EQ(gate->shares(), 1u); }
