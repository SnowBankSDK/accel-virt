#include "executor/egl_entry_gate.hpp"
#include "executor/host_lifecycle_gate.hpp"
#include "executor/host_lifecycle_gates.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using av::EglEntry;
using av::EglEntryGate;

namespace {

constexpr std::uint64_t long_wait_ns = 5000000000ull;
constexpr std::uint64_t short_wait_ns = 50000000ull;

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(40)); }

}

TEST(egl_entry_gate, a_gate_no_driver_needs_lets_entries_through_while_it_is_excluded_and_counts_nothing)
{ static EglEntryGate gate;
  CHECK(!gate.needed());
  CHECK(gate.exclude(short_wait_ns));
  std::thread entry([&] {
      const EglEntry entered(gate);
      CHECK_EQ(gate.inside(), 0u);
  });
  entry.join();
  gate.admit();
  CHECK_EQ(gate.exclusions(), 0u);
  CHECK_EQ(gate.held_back(), 0u); }

TEST(egl_entry_gate, an_exclusion_waits_for_an_entry_that_is_inside_and_starts_once_it_left)
{ static EglEntryGate gate;
  gate.need();
  std::atomic<bool> inside{false}, leave{false}, excluded{false};
  std::thread       entry([&] {
      const EglEntry entered(gate);
      inside = true;
      while (!leave.load()) std::this_thread::yield();
  });
  while (!inside.load()) std::this_thread::yield();
  CHECK_EQ(gate.inside(), 1u);
  std::thread destroyer([&] {
      CHECK(gate.exclude(long_wait_ns));
      excluded = true;
      CHECK_EQ(gate.inside(), 0u);
      gate.admit();
  });
  settle();
  CHECK(!excluded.load());
  leave = true;
  entry.join();
  destroyer.join();
  CHECK(excluded.load());
  CHECK_EQ(gate.exclusions(), 1u); }

TEST(egl_entry_gate, an_entry_that_comes_during_an_exclusion_stays_outside_until_it_ends)
{ static EglEntryGate gate;
  gate.need();
  std::atomic<bool> entered{false};
  std::thread       destroyer_side;
  CHECK(gate.exclude(long_wait_ns));
  std::thread entry([&] {
      const EglEntry inside(gate);
      entered = true;
  });
  settle();
  CHECK(!entered.load());
  CHECK_EQ(gate.inside(), 0u);
  gate.admit();
  entry.join();
  CHECK(entered.load());
  CHECK_EQ(gate.held_back(), 1u); }

TEST(egl_entry_gate, the_thread_that_excludes_enters_freely_and_a_nested_entry_never_waits_for_an_exclusion_that_waits_for_it)
{ static EglEntryGate gate;
  gate.need();
  std::thread excluder([&] {
      CHECK(gate.exclude(long_wait_ns));
      { const EglEntry own(gate);
        CHECK_EQ(gate.inside(), 1u); }
      gate.admit();
  });
  excluder.join();
  std::atomic<bool> outer{false}, nest{false}, nested{false};
  std::thread       entry([&] {
      const EglEntry first(gate);
      outer = true;
      while (!nest.load()) std::this_thread::yield();
      { const EglEntry second(gate);
        nested = true; }
  });
  while (!outer.load()) std::this_thread::yield();
  std::thread destroyer([&] {
      CHECK(gate.exclude(long_wait_ns));
      gate.admit();
  });
  settle();
  nest = true;
  entry.join();
  destroyer.join();
  CHECK(nested.load());
  CHECK_EQ(gate.inside(), 0u); }

TEST(egl_entry_gate, an_exclusion_that_an_entry_never_leaves_room_for_is_refused_in_time_and_entries_go_on)
{ static EglEntryGate gate;
  gate.need();
  std::atomic<bool> inside{false}, leave{false};
  std::thread       entry([&] {
      const EglEntry entered(gate);
      inside = true;
      while (!leave.load()) std::this_thread::yield();
  });
  while (!inside.load()) std::this_thread::yield();
  bool        excluded = true;
  std::thread destroyer([&] { excluded = gate.exclude(short_wait_ns); });
  destroyer.join();
  CHECK(!excluded);
  std::thread other([&] { const EglEntry entered(gate); });
  other.join();
  leave = true;
  entry.join();
  CHECK_EQ(gate.exclusions(), 0u); }

TEST(egl_entry_gate, a_thread_that_retired_keeps_exclusions_out_until_its_joiner_says_it_is_gone_and_one_on_a_gate_nothing_needs_keeps_nothing)
{ static EglEntryGate gate;
  void       *kept = &gate;
  std::thread idle([&] { kept = gate.retire(); });
  idle.join();
  CHECK(kept == nullptr);
  gate.need();
  std::atomic<bool> left{false}, end{false};
  std::thread       leaving([&] {
      { const EglEntry entered(gate); }
      kept = gate.retire();
      left = true;
      while (!end.load()) std::this_thread::yield();
  });
  while (!left.load()) std::this_thread::yield();
  CHECK(kept != nullptr);
  CHECK_EQ(gate.inside(), 1u);
  std::atomic<bool> excluded{false};
  std::thread       destroyer([&] {
      CHECK(gate.exclude(long_wait_ns));
      excluded = true;
      gate.admit();
  });
  settle();
  CHECK(!excluded.load());
  gate.joined(kept);
  destroyer.join();
  CHECK(excluded.load());
  CHECK_EQ(gate.inside(), 0u);
  end = true;
  leaving.join();
  std::thread during([&] {
      CHECK(gate.exclude(long_wait_ns));
      std::atomic<bool> retired{false};
      std::thread       late([&] {
          kept = gate.retire();
          retired = true;
      });
      settle();
      CHECK(!retired.load());
      gate.admit();
      late.join();
      CHECK(retired.load());
  });
  during.join();
  gate.joined(kept);
  gate.joined(nullptr);
  CHECK_EQ(gate.inside(), 0u); }

TEST(egl_entry_gate, a_thread_that_retired_and_is_never_joined_keeps_exclusions_out_while_it_lives_and_lets_them_in_once_it_is_gone)
{ static EglEntryGate      gate;
  static std::atomic<bool> retired{false}, end{false};
  static void             *kept = nullptr;
  gate.need();
  std::thread abandoned([] {
      { const EglEntry entered(gate); }
      kept = gate.retire();
      retired = true;
      while (!end.load()) std::this_thread::yield();
  });
  abandoned.detach();
  while (!retired.load()) std::this_thread::yield();
  CHECK(kept != nullptr);
  CHECK(!gate.exclude(short_wait_ns));
  CHECK(!gate.exclude(short_wait_ns));
  CHECK_EQ(gate.inside(), 1u);
  end = true;
  CHECK(gate.exclude(long_wait_ns));
  CHECK_EQ(gate.inside(), 0u);
  gate.admit();
  std::thread next([] {
      { const EglEntry entered(gate); }
      CHECK_EQ(gate.inside(), 0u);
  });
  next.join();
  gate.joined(kept);
  CHECK_EQ(gate.inside(), 0u);
  CHECK(gate.exclude(short_wait_ns));
  gate.admit(); }

TEST(egl_entry_gate, only_a_gate_of_the_driver_that_needs_it_excludes_egl_entries_and_another_drivers_entries_are_held_out_too)
{ static EglEntryGate egl;
  static av::HostLifecycleGates gates(egl);
  const std::uint8_t mesa_gpu[16] = {1}, vendor_gpu[16] = {2};
  av::HostLifecycleGate *const mesa = gates.add(mesa_gpu, av::HostDriver(VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, "mesa", "1"));
  CHECK(mesa != nullptr);
  CHECK(!mesa->excludes_egl());
  CHECK(!egl.needed());
  av::HostLifecycleGate *const vendor = gates.add(vendor_gpu, av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "vendor", "1"));
  CHECK(vendor != nullptr);
  CHECK(vendor != mesa);
  CHECK(vendor->excludes_egl());
  CHECK(egl.needed());
  std::atomic<bool> entered{false};
  CHECK(mesa->enter());
  std::thread through([&] { const EglEntry inside(egl); });
  through.join();
  mesa->leave();
  CHECK(vendor->enter());
  std::thread entry([&] {
      const av::HostLifecycleHold own_driver(mesa);
      const EglEntry              inside(egl);
      entered = true;
  });
  settle();
  CHECK(!entered.load());
  vendor->leave();
  entry.join();
  CHECK(entered.load());
  CHECK_EQ(egl.exclusions(), 1u); }

TEST(egl_entry_gate, an_uncontended_entry_costs_a_few_nanoseconds_when_needed_and_less_when_not)
{ static EglEntryGate needed;
  static EglEntryGate idle;
  needed.need();
  constexpr std::uint32_t rounds = 20000000u;
  double                  ns[2] = {};
  std::thread             bench([&] {
      EglEntryGate *const gates[2] = {&needed, &idle};
      for (std::uint32_t which = 0u; which < 2u; which++) {
          const auto began = std::chrono::steady_clock::now();
          for (std::uint32_t round = 0u; round < rounds; round++) {
              gates[which]->enter();
              gates[which]->leave();
          }
          const auto ended = std::chrono::steady_clock::now();
          ns[which] = std::chrono::duration<double, std::nano>(ended - began).count() / rounds;
      }
  });
  bench.join();
  std::fprintf(stderr, "egl entry gate: %.2f ns per uncontended enter+leave when needed, %.2f ns when not\n", ns[0], ns[1]);
  CHECK(ns[0] < 200.0);
  CHECK(ns[1] <= ns[0] + 5.0); }
