#include "executor/d3d/leased_objects.hpp"
#include "executor/session_lease.hpp"
#include "executor/shim/host_heap.hpp"
#include "executor/vram_ledger.hpp"
#include "harness/framework/test_registry.hpp"
#include "proto/submission/lease_record.hpp"

#include <cstdint>

namespace {

constexpr std::uint64_t mib = std::uint64_t{1} << 20u;
constexpr av::LeaseRecord::Kind device = av::LeaseRecord::Kind::device;

}

TEST(leased_objects, a_recharge_holds_back_what_an_object_grows_by_and_restores_what_it_shrinks_by)
{
    av::HostHeap   heap;
    av::VramLedger ledger;
    ledger.learn(std::uint64_t{8} << 30u);
    av::SessionLease lease;
    lease.bind(&ledger, nullptr);
    CHECK_EQ(lease.extend(0u, 64u * mib, 0u), 64u * mib);
    av::LeasedObjects leased(heap);
    leased.draw_from(&lease);
    leased.charge(7u, 16u * mib);
    leased.settle();
    CHECK_EQ(lease.idle(device), 48u * mib);
    leased.recharge(7u, 40u * mib);
    leased.settle();
    CHECK_EQ(lease.idle(device), 24u * mib);
    CHECK_EQ(leased.charged(7u), 40u * mib);
    leased.recharge(7u, 8u * mib);
    CHECK_EQ(lease.idle(device), 56u * mib);
    leased.recharge(9u, 4u * mib);
    leased.settle();
    CHECK_EQ(lease.idle(device), 52u * mib);
    leased.release(7u);
    leased.release(9u);
    CHECK_EQ(lease.idle(device), 64u * mib);
    CHECK_EQ(leased.count(), 0u);
    leased.draw_from(nullptr);
    static_cast<void>(lease.close());
}
