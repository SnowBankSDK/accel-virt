#include "executor/host_lifecycle_gate.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using av::HostLifecycleGate;
using av::HostLifecycleHold;

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
