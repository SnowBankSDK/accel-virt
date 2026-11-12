#include "executor/host_ram_ledger.hpp"
#include "executor/session_lease.hpp"
#include "executor/session_reservation.hpp"
#include "executor/vram_account.hpp"
#include "executor/vram_ledger.hpp"
#include "harness/executor/fake_memory_gauge.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/submission/lease_record.hpp"

#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using av::FakeMemoryGauge;
using av::HostRamLedger;
using av::LeaseRecord;
using av::SessionLease;
using av::SessionReservation;
using av::VramAccount;
using av::VramLedger;

namespace {

constexpr std::uint64_t mib = 1ull << 20;
constexpr std::uint64_t gib = 1ull << 30;
constexpr std::uint32_t device = static_cast<std::uint32_t>(LeaseRecord::Kind::device);
constexpr std::uint32_t host = static_cast<std::uint32_t>(LeaseRecord::Kind::host);

struct Session {
    explicit Session(VramLedger &ledger) : account(ledger)
    {
        lease.bind(&ledger, nullptr);
        account.reserve_from(&lease);
    }

    SessionLease lease;
    VramAccount  account;
};

}

TEST(session_lease, a_grant_takes_its_bytes_from_the_ledger_and_holds_them_idle)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  SessionLease lease;
  lease.bind(&ledger, nullptr);
  CHECK_EQ(lease.extend(device, 64 * mib, 64 * mib), 64 * mib);
  CHECK_EQ(lease.idle(LeaseRecord::Kind::device), 64 * mib);
  CHECK_EQ(ledger.used(), 64 * mib); }

TEST(session_lease, a_grant_the_ledger_cannot_hold_falls_back_to_the_least_and_then_to_nothing)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  VramAccount other(ledger);
  CHECK(other.take(ledger.advertised() - 100 * mib));
  SessionLease lease;
  lease.bind(&ledger, nullptr);
  CHECK_EQ(lease.extend(device, 256 * mib, 40 * mib), 40 * mib);
  CHECK_EQ(lease.extend(device, 256 * mib, 80 * mib), 0u);
  CHECK_EQ(lease.idle(LeaseRecord::Kind::device), 40 * mib);
  CHECK(ledger.used() <= ledger.cap()); }

TEST(session_lease, an_unknown_kind_is_granted_nothing)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  SessionLease lease;
  lease.bind(&ledger, nullptr);
  CHECK_EQ(lease.extend(LeaseRecord::kinds, mib, mib), 0u);
  CHECK_EQ(ledger.used(), 0u); }

TEST(session_lease, opening_grants_the_initial_lease_of_every_kind)
{ VramLedger vram;
  vram.learn(8 * gib);
  HostRamLedger ram;
  ram.budget(8 * gib);
  SessionLease lease;
  lease.bind(&vram, &ram);
  lease.open();
  CHECK_EQ(lease.idle(LeaseRecord::Kind::device), LeaseRecord::initial_bytes[device]);
  CHECK_EQ(lease.idle(LeaseRecord::Kind::host), LeaseRecord::initial_bytes[host]);
  CHECK_EQ(vram.used(), LeaseRecord::initial_bytes[device]);
  CHECK_EQ(ram.used(), LeaseRecord::initial_bytes[host]); }

TEST(session_lease, a_gate_take_draws_on_the_reservation_first_and_the_ledger_only_for_the_rest)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  Session session(ledger);
  CHECK_EQ(session.lease.extend(device, 64 * mib, 0u), 64 * mib);
  CHECK(session.account.take(48 * mib));
  CHECK_EQ(ledger.used(), 64 * mib);
  CHECK_EQ(session.lease.idle(LeaseRecord::Kind::device), 16 * mib);
  CHECK(session.account.take(32 * mib));
  CHECK_EQ(ledger.used(), 80 * mib);
  CHECK_EQ(session.lease.idle(LeaseRecord::Kind::device), 0u); }

TEST(session_lease, a_take_covered_by_the_reservation_succeeds_when_the_ledger_is_full)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  Session session(ledger);
  CHECK_EQ(session.lease.extend(device, 64 * mib, 0u), 64 * mib);
  VramAccount other(ledger);
  CHECK(other.take(ledger.cap() - ledger.used()));
  CHECK(!other.take(1u));
  CHECK(session.account.take(64 * mib));
  CHECK(!session.account.take(1u));
  CHECK_EQ(ledger.used(), ledger.cap()); }

TEST(session_lease, a_refused_take_puts_back_what_it_drew)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  Session session(ledger);
  CHECK_EQ(session.lease.extend(device, 64 * mib, 0u), 64 * mib);
  VramAccount other(ledger);
  CHECK(other.take(ledger.cap() - ledger.used()));
  CHECK(!session.account.take(65 * mib));
  CHECK_EQ(session.lease.idle(LeaseRecord::Kind::device), 64 * mib);
  CHECK_EQ(session.account.held(), 0u); }

TEST(session_lease, a_give_keeps_the_bytes_reserved_and_a_surrender_returns_them)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  Session session(ledger);
  CHECK_EQ(session.lease.extend(device, 64 * mib, 0u), 64 * mib);
  CHECK(session.account.take(64 * mib));
  session.account.give(64 * mib);
  CHECK_EQ(ledger.used(), 64 * mib);
  CHECK_EQ(session.lease.idle(LeaseRecord::Kind::device), 64 * mib);
  session.lease.surrender(device, 48 * mib);
  CHECK_EQ(ledger.used(), 16 * mib);
  session.lease.surrender(device, 48 * mib);
  CHECK_EQ(ledger.used(), 0u);
  CHECK_EQ(session.lease.idle(LeaseRecord::Kind::device), 0u); }

TEST(session_lease, closing_returns_everything_and_later_gives_reach_the_ledger)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  Session session(ledger);
  CHECK_EQ(session.lease.extend(device, 128 * mib, 0u), 128 * mib);
  CHECK(session.account.take(32 * mib));
  CHECK_EQ(session.lease.close(), 96 * mib);
  CHECK_EQ(ledger.used(), 32 * mib);
  CHECK_EQ(session.lease.extend(device, mib, 0u), 0u);
  session.account.give(32 * mib);
  CHECK_EQ(ledger.used(), 0u); }

TEST(session_lease, a_draw_before_a_shared_device_allocates_makes_room_for_it_and_a_restore_holds_it_back)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  SessionLease lease;
  lease.bind(&ledger, nullptr);
  CHECK_EQ(lease.extend(device, 64 * mib, 0u), 64 * mib);
  VramAccount shared(ledger);
  CHECK(shared.take(ledger.cap() - ledger.used()));
  VramAccount gate(ledger);
  CHECK(!gate.take(40 * mib));
  lease.of(LeaseRecord::Kind::device).draw(40 * mib);
  CHECK(gate.take(40 * mib));
  CHECK_EQ(lease.idle(LeaseRecord::Kind::device), 24 * mib);
  lease.of(LeaseRecord::Kind::device).restore(40 * mib);
  CHECK_EQ(lease.idle(LeaseRecord::Kind::device), 64 * mib);
  gate.give(40 * mib);
  CHECK_EQ(ledger.used(), ledger.cap());
  CHECK_EQ(lease.close(), 64 * mib);
  shared.give(shared.held());
  CHECK_EQ(ledger.used(), 0u); }

TEST(session_lease, an_integrated_gpus_device_reservation_also_holds_host_ram_and_gives_both_back)
{ HostRamLedger ram;
  ram.budget(4 * gib);
  VramLedger vram;
  vram.learn(8 * gib);
  vram.share_host_ram(&ram);
  SessionLease lease;
  lease.bind(&vram, &ram);
  CHECK_EQ(lease.extend(device, 256 * mib, 0u), 256 * mib);
  CHECK_EQ(lease.extend(host, 128 * mib, 0u), 128 * mib);
  CHECK_EQ(vram.used(), 256 * mib);
  CHECK_EQ(ram.used(), 384 * mib);
  CHECK_EQ(lease.close(), 384 * mib);
  CHECK_EQ(vram.used(), 0u);
  CHECK_EQ(ram.used(), 0u); }

TEST(session_lease, a_host_ram_grant_is_refused_by_the_floor_and_withheld_from_the_gauge_once_granted)
{ FakeMemoryGauge gauge;
  gauge.free_bytes = 2 * gib;
  HostRamLedger ram;
  ram.budget(16 * gib);
  ram.watch(&gauge);
  ram.floor(gib);
  SessionLease lease;
  lease.bind(nullptr, &ram);
  CHECK_EQ(lease.extend(host, 2 * gib, 512 * mib), 512 * mib);
  CHECK(ram.admissible() <= 512 * mib);
  VramAccount other(ram);
  CHECK(!other.take(600 * mib));
  CHECK_EQ(lease.of(LeaseRecord::Kind::host).admissible(), ram.admissible() + 512 * mib); }

TEST(session_lease, a_model_of_several_sessions_never_takes_the_ledger_past_its_cap)
{ for (std::uint32_t seed = 1u; seed <= 16u; seed++) {
      VramLedger ledger;
      ledger.learn(4 * gib);
      std::vector<std::unique_ptr<Session>> sessions;
      for (std::uint32_t index = 0u; index < 6u; index++) sessions.push_back(std::make_unique<Session>(ledger));
      std::vector<std::vector<std::uint64_t>> objects(sessions.size());
      std::mt19937 random(seed);
      for (std::uint32_t step = 0u; step < 4000u; step++) {
          const std::size_t at = random() % sessions.size();
          Session          &session = *sessions[at];
          const std::uint64_t bytes = (1u + random() % 64u) * mib;
          switch (random() % 4u) {
          case 0u: static_cast<void>(session.lease.extend(device, bytes * 4u, bytes)); break;
          case 1u:
              if (session.account.take(bytes)) objects[at].push_back(bytes);
              break;
          case 2u:
              if (!objects[at].empty()) {
                  session.account.give(objects[at].back());
                  objects[at].pop_back();
              }
              break;
          default: session.lease.surrender(device, bytes); break;
          }
          CHECK(ledger.used() <= ledger.cap());
      }
      for (std::size_t index = 0u; index < sessions.size(); index++) {
          for (const std::uint64_t bytes : objects[index]) sessions[index]->account.give(bytes);
          static_cast<void>(sessions[index]->lease.close());
      }
      CHECK_EQ(ledger.used(), 0u);
  } }

TEST(session_lease, create_and_destroy_loops_on_several_threads_return_the_ledger_to_where_it_started)
{ VramLedger ledger;
  ledger.learn(8 * gib);
  VramAccount resident(ledger);
  CHECK(resident.take(gib));
  const std::uint64_t start = ledger.used();
  std::vector<std::unique_ptr<Session>> sessions;
  for (std::uint32_t index = 0u; index < 4u; index++) sessions.push_back(std::make_unique<Session>(ledger));
  for (std::uint32_t round = 0u; round < 4u; round++) {
      std::vector<std::thread> threads;
      for (std::uint32_t index = 0u; index < sessions.size(); index++)
          threads.emplace_back([&session = *sessions[index], index] {
              std::mt19937 random(index + 1u);
              for (std::uint32_t step = 0u; step < 2000u; step++) {
                  const std::uint64_t bytes = (1u + random() % 16u) * mib;
                  if (step % 7u == 0u) static_cast<void>(session.lease.extend(device, 32 * mib, 0u));
                  if (session.account.take(bytes)) session.account.give(bytes);
                  if (step % 11u == 0u) session.lease.surrender(device, 16 * mib);
                  SessionReservation &reservation = session.lease.of(LeaseRecord::Kind::device);
                  reservation.draw(bytes);
                  reservation.restore(bytes);
              }
          });
      for (std::thread &thread : threads) thread.join();
      CHECK(ledger.used() <= ledger.cap());
  }
  for (const std::unique_ptr<Session> &session : sessions) static_cast<void>(session->lease.close());
  CHECK_EQ(ledger.used(), start);
  for (const std::unique_ptr<Session> &session : sessions) CHECK_EQ(session->account.held(), 0u); }
