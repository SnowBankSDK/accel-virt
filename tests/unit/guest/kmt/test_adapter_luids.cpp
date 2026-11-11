#include "core/support/status.hpp"
#include "guest/kmt/adapter_luids.hpp"
#include "guest/kmt/pci_identity.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/guest/mock_kmt.hpp"

#include <cstdint>

using av::AdapterLuids;
using av::MockKmt;
using av::PciIdentity;
using av::Status;

namespace {

const PciIdentity on_radeon(0x1002u, 0x7550u, 0x1AF4u, 0x5256u);

}

TEST(adapter_luids, a_thousand_handles_opened_and_closed_leave_no_entry_behind)
{ MockKmt kmt;
  AdapterLuids luids;
  for (std::uint32_t round = 0u; round < 1000u; round++) {
    const std::uint32_t handle = 0x7F000000u + round * 0x40u;
    luids.remember(handle, 0x30518u);
    std::uint64_t luid = 0u;
    CHECK_STATUS(luids.luid_of(kmt, handle, luid), Status::ok);
    CHECK_EQ(luid, 0x30518u);
    luids.forget(handle);
    CHECK_EQ(luids.count(), 0u);
  }
  CHECK(luids.invariants_hold()); }

TEST(adapter_luids, a_full_table_still_names_a_new_handle_through_the_kernel)
{ MockKmt kmt;
  kmt.add_adapter(on_radeon, 3200u);
  kmt.set_adapter_luid(0u, 0x30518u);
  AdapterLuids luids;
  for (std::uint32_t entry = 0u; entry < AdapterLuids::capacity; entry++)
    luids.remember(0x7F000000u + entry * 0x40u, 0x1000u + entry);
  CHECK_EQ(luids.count(), AdapterLuids::capacity);
  std::uint32_t handles[1] = {};
  std::uint32_t count = 0u;
  CHECK_STATUS(kmt.enumerate_adapters(handles, 1u, count), Status::ok);
  CHECK_EQ(count, 1u);
  std::uint64_t luid = 0u;
  CHECK_STATUS(luids.luid_of(kmt, handles[0], luid), Status::ok);
  CHECK_EQ(luid, 0x30518u);
  CHECK_STATUS(luids.luid_of(kmt, 0x7F000000u, luid), Status::ok);
  CHECK_EQ(luid, 0x1000u);
  CHECK(luids.invariants_hold());
  CHECK_STATUS(kmt.close_adapter(handles[0]), Status::ok); }

TEST(adapter_luids, a_handle_the_kernel_cannot_name_has_no_luid)
{ MockKmt kmt;
  AdapterLuids luids;
  std::uint64_t luid = 7u;
  CHECK(luids.luid_of(kmt, 0x1234u, luid) != Status::ok);
  CHECK_EQ(luid, 0u); }
