#include "executor/session_lease.hpp"
#include "executor/vram_account.hpp"
#include "executor/vram_ledger.hpp"
#include "guest/transport/memory_lease.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/transport/lease_host_transport.hpp"
#include "proto/submission/lease_record.hpp"

#include <cstdint>
#include <random>
#include <vector>

using av::LeaseHostTransport;
using av::LeaseRecord;
using av::MemoryLease;
using av::SessionLease;
using av::VramAccount;
using av::VramLedger;

namespace {

constexpr std::uint64_t mib = 1ull << 20;
constexpr std::uint64_t gib = 1ull << 30;
constexpr LeaseRecord::Kind device = LeaseRecord::Kind::device;

struct Rig {
    explicit Rig(std::uint64_t cap) : transport(host), guest(transport), gate(ledger)
    {
        ledger.learn(cap);
        host.bind(&ledger, nullptr);
        host.open();
        gate.reserve_from(&host);
    }

    bool create(std::uint64_t bytes)
    {
        if (!guest.take(device, bytes)) return false;
        return gate.take(bytes);
    }

    void destroy(std::uint64_t bytes)
    {
        gate.give(bytes);
        guest.give(device, bytes);
    }

    VramLedger         ledger;
    SessionLease       host;
    LeaseHostTransport transport;
    MemoryLease        guest;
    VramAccount        gate;
};

}

TEST(memory_lease, the_initial_lease_admits_small_creates_without_asking_the_host)
{ Rig rig(8 * gib);
  CHECK(rig.create(LeaseRecord::initial_bytes / 2u));
  CHECK(rig.create(LeaseRecord::initial_bytes / 2u));
  CHECK_EQ(rig.transport.extensions(), 0u);
  CHECK_EQ(rig.guest.left(device), 0u);
  CHECK(rig.guest.invariants_hold()); }

TEST(memory_lease, a_burst_of_creates_asks_the_host_a_logarithmic_number_of_times)
{ Rig rig(8 * gib);
  for (std::uint32_t index = 0u; index < 1024u; index++) CHECK(rig.create(mib));
  CHECK(rig.transport.extensions() <= 10u);
  CHECK(rig.transport.extensions() >= 3u);
  CHECK_EQ(rig.guest.left(device), rig.host.idle(device));
  CHECK_EQ(rig.ledger.used(), 1024u * mib + rig.host.idle(device)); }

TEST(memory_lease, a_create_the_host_cannot_cover_is_refused_and_takes_nothing)
{ Rig rig(gib);
  VramAccount other(rig.ledger);
  CHECK(other.take(rig.ledger.cap() - rig.ledger.used() - 32 * mib));
  CHECK(rig.create(LeaseRecord::initial_bytes));
  CHECK(!rig.guest.take(device, 64 * mib));
  CHECK_EQ(rig.transport.extensions(), 1u);
  CHECK_EQ(rig.transport.returns(), 0u);
  CHECK_EQ(rig.transport.others(), 0u);
  CHECK(rig.guest.take(device, 16 * mib));
  CHECK(rig.ledger.used() <= rig.ledger.cap()); }

TEST(memory_lease, a_refused_extension_answers_out_of_memory_without_a_return)
{ Rig rig(8 * gib);
  rig.transport.refuse_all(true);
  CHECK(!rig.guest.take(device, 64 * mib));
  CHECK_EQ(rig.guest.left(device), LeaseRecord::initial_bytes);
  rig.transport.refuse_all(false);
  CHECK(rig.guest.take(device, 64 * mib)); }

TEST(memory_lease, a_destroy_credits_the_lease_so_the_next_create_needs_no_round_trip)
{ Rig rig(8 * gib);
  CHECK(rig.create(64 * mib));
  const std::uint32_t asked = rig.transport.extensions();
  rig.destroy(64 * mib);
  for (std::uint32_t round = 0u; round < 100u; round++) {
      CHECK(rig.create(64 * mib));
      rig.destroy(64 * mib);
  }
  CHECK_EQ(rig.transport.extensions(), asked);
  CHECK_EQ(rig.guest.left(device), rig.host.idle(device)); }

TEST(memory_lease, lease_above_the_high_water_mark_goes_back_to_the_host_without_waiting)
{ Rig rig(8 * gib);
  std::vector<std::uint64_t> made;
  for (std::uint32_t index = 0u; index < 64u; index++) {
      CHECK(rig.create(32 * mib));
      made.push_back(32 * mib);
  }
  const std::uint64_t peak = rig.ledger.used();
  for (const std::uint64_t bytes : made) rig.destroy(bytes);
  CHECK(rig.transport.returns() >= 1u);
  CHECK(rig.guest.left(device) <= MemoryLease::largest_chunk * MemoryLease::kept_chunks);
  CHECK_EQ(rig.guest.left(device), rig.host.idle(device));
  CHECK_EQ(rig.ledger.used(), rig.host.idle(device));
  CHECK(rig.ledger.used() < peak); }

TEST(memory_lease, guest_and_host_agree_on_the_lease_through_a_seeded_mix_of_creates_and_destroys)
{ for (std::uint32_t seed = 1u; seed <= 8u; seed++) {
      Rig rig(2 * gib);
      std::vector<std::uint64_t> live;
      std::mt19937 random(seed);
      for (std::uint32_t step = 0u; step < 3000u; step++) {
          const std::uint64_t bytes = (1u + random() % 96u) * mib;
          if (random() % 3u != 0u) {
              if (rig.create(bytes)) live.push_back(bytes);
          } else if (!live.empty()) {
              const std::size_t at = random() % live.size();
              rig.destroy(live[at]);
              live[at] = live.back();
              live.pop_back();
          }
          CHECK_EQ(rig.guest.left(device), rig.host.idle(device));
          CHECK(rig.ledger.used() <= rig.ledger.cap());
      }
      for (const std::uint64_t bytes : live) rig.destroy(bytes);
      static_cast<void>(rig.host.close());
      CHECK_EQ(rig.ledger.used(), 0u);
  } }
