#include "guest/kmt/adapter_slots.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstdint>

using av::AdapterSlots;

TEST(adapter_slots, a_thousand_devices_made_and_closed_on_one_adapter_stay_in_one_slot)
{ AdapterSlots slots;
  std::uint32_t first = AdapterSlots::none;
  for (std::uint32_t round = 0u; round < 1000u; round++) {
    bool fresh = false;
    const std::uint32_t slot = slots.claim(0x30518u, fresh);
    CHECK(slot != AdapterSlots::none);
    CHECK(fresh);
    if (round == 0u) first = slot;
    CHECK_EQ(slot, first);
    slots.hold(slot);
    CHECK_EQ(slots.used(), 1u);
    CHECK(slots.release(slot));
    CHECK_EQ(slots.used(), 0u);
    CHECK(slots.invariants_hold());
  } }

TEST(adapter_slots, live_devices_on_one_adapter_share_its_slot_until_the_last_closes)
{ AdapterSlots slots;
  constexpr std::uint32_t devices = 64u;
  bool fresh = false;
  const std::uint32_t slot = slots.claim(0x30518u, fresh);
  CHECK(fresh);
  for (std::uint32_t device = 0u; device < devices; device++) {
    CHECK_EQ(slots.claim(0x30518u, fresh), slot);
    CHECK(!fresh);
    slots.hold(slot);
  }
  CHECK_EQ(slots.used(), 1u);
  CHECK_EQ(slots.holds(slot), devices);
  for (std::uint32_t device = 1u; device < devices; device++) CHECK(!slots.release(slot));
  CHECK_EQ(slots.luid(slot), 0x30518u);
  CHECK(slots.release(slot));
  CHECK_EQ(slots.luid(slot), 0u);
  CHECK(!slots.release(slot));
  CHECK(slots.invariants_hold()); }

TEST(adapter_slots, an_adapter_without_devices_gives_its_slot_to_another_adapter)
{ AdapterSlots slots;
  bool fresh = false;
  for (std::uint64_t luid = 1u; luid <= AdapterSlots::capacity; luid++) {
    const std::uint32_t slot = slots.claim(luid, fresh);
    CHECK(slot != AdapterSlots::none);
    if (luid != 7u) slots.hold(slot);
  }
  const std::uint32_t taken = slots.claim(0x99u, fresh);
  CHECK(taken != AdapterSlots::none);
  CHECK(fresh);
  CHECK_EQ(slots.luid(taken), 0x99u);
  CHECK_EQ(slots.used(), AdapterSlots::capacity);
  CHECK(slots.invariants_hold()); }

TEST(adapter_slots, a_new_adapter_is_refused_only_while_every_slot_holds_devices)
{ AdapterSlots slots;
  bool fresh = false;
  for (std::uint64_t luid = 1u; luid <= AdapterSlots::capacity; luid++) slots.hold(slots.claim(luid, fresh));
  CHECK_EQ(slots.claim(0x99u, fresh), AdapterSlots::none);
  CHECK(!fresh);
  CHECK_EQ(slots.claim(0u, fresh), AdapterSlots::none);
  const std::uint32_t known = slots.claim(3u, fresh);
  CHECK(known != AdapterSlots::none);
  CHECK(!fresh);
  CHECK(slots.release(known));
  CHECK_EQ(slots.claim(0x99u, fresh), known);
  CHECK(fresh);
  CHECK(slots.invariants_hold()); }

TEST(adapter_slots, a_slot_whose_learning_failed_is_vacated)
{ AdapterSlots slots;
  bool fresh = false;
  const std::uint32_t slot = slots.claim(0x40u, fresh);
  slots.vacate(slot);
  CHECK_EQ(slots.used(), 0u);
  CHECK_EQ(slots.claim(0x40u, fresh), slot);
  CHECK(fresh);
  CHECK(slots.invariants_hold()); }
