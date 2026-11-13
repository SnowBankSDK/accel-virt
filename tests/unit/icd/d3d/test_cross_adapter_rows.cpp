#include "core/support/status.hpp"
#include "harness/framework/test_registry.hpp"
#include "icd/d3d/cross_adapter_rows.hpp"
#include "icd/d3d/kernel_surface.hpp"
#include "icd/d3d/page_lender.hpp"
#include "icd/d3d/surface_allocations.hpp"
#include "proto/resources/allocation_descriptor.hpp"
#include "proto/resources/allocation_kind.hpp"

#include <cstdint>

using av::AllocationDescriptor;
using av::AllocationKind;
using av::HostAllocation;
using av::Status;
using av::d3d::CrossAdapterRows;
using av::d3d::Hresult;
using av::d3d::KernelSurface;

namespace {

constexpr std::uint32_t b8g8r8a8 = 87u;
constexpr std::uint32_t handle = 0x40000a40u;
constexpr Hresult       kernel_refusal = static_cast<Hresult>(0x8876017cu);

class FakeAllocations final : public av::d3d::SurfaceAllocations {
public:
    Hresult make_surface(KernelSurface &surface, void *resource, std::uint32_t &allocation) override
    {
        made_++;
        resource_ = resource;
        CHECK_STATUS(described_.decode(surface.allocation_data(), AllocationDescriptor::encoded_size), Status::ok);
        across_ = surface.across_adapters();
        if (refuse_) return kernel_refusal;
        allocation = handle;
        return av::d3d::hr_ok;
    }
    void return_surface(std::uint32_t allocation, void *resource) override
    {
        returned_++;
        CHECK_EQ(allocation, handle);
        CHECK(resource == resource_);
    }

    AllocationDescriptor described_;
    void                *resource_ = nullptr;
    std::uint32_t        made_ = 0u, returned_ = 0u;
    bool                 across_ = false, refuse_ = false;
};

class FakeLender final : public av::d3d::PageLender {
public:
    Status lend(std::uint32_t allocation, std::uint64_t bytes, HostAllocation *&made) const override
    {
        lent_++;
        allocation_ = allocation;
        bytes_ = bytes;
        made = refuse_ ? nullptr : pages();
        return refuse_ ? Status::no_memory : Status::ok;
    }
    static HostAllocation *pages() { return reinterpret_cast<HostAllocation *>(std::uintptr_t{0x7000}); }

    mutable std::uint32_t lent_ = 0u, allocation_ = 0u;
    mutable std::uint64_t bytes_ = 0u;
    bool                  refuse_ = false;
};

}

TEST(cross_adapter_rows, a_made_surface_is_a_cross_adapter_allocation_whose_whole_rows_are_lent_once)
{
    FakeAllocations  allocations;
    FakeLender       lender;
    int              runtime = 0;
    CrossAdapterRows rows(allocations, lender);
    CHECK_EQ(rows.make(160u, 120u, b8g8r8a8, &runtime), av::d3d::hr_ok);
    CHECK_EQ(allocations.made_, 1u);
    CHECK(allocations.across_);
    CHECK(allocations.resource_ == &runtime);
    CHECK(allocations.described_.kind() == AllocationKind::cross_adapter);
    CHECK(allocations.described_.cpu_visible());
    CHECK_EQ(allocations.described_.layout().width(), 160u);
    CHECK_EQ(allocations.described_.layout().height(), 120u);
    CHECK_EQ(lender.lent_, 1u);
    CHECK_EQ(lender.allocation_, handle);
    CHECK_EQ(lender.bytes_, allocations.described_.size());
    CHECK(lender.bytes_ >= std::uint64_t{119u} * rows.pitch() + 160u * 4u);
    CHECK_EQ(rows.allocation(), handle);
    CHECK_EQ(rows.pitch(), allocations.described_.layout().pitch());
    CHECK(rows.pitch() >= 160u * 4u);
    CHECK(rows.lent() == FakeLender::pages());
    CHECK_EQ(allocations.returned_, 0u);
}

TEST(cross_adapter_rows, pages_that_cannot_be_lent_give_the_allocation_back_and_report_no_memory)
{
    FakeAllocations allocations;
    FakeLender      lender;
    lender.refuse_ = true;
    int              runtime = 0;
    CrossAdapterRows rows(allocations, lender);
    CHECK_EQ(rows.make(64u, 64u, b8g8r8a8, &runtime), av::d3d::hr_out_of_memory);
    CHECK_EQ(allocations.made_, 1u);
    CHECK_EQ(allocations.returned_, 1u);
    CHECK_EQ(rows.allocation(), 0u);
    CHECK(rows.lent() == nullptr);
}

TEST(cross_adapter_rows, what_the_kernel_refuses_or_cannot_be_rows_lends_nothing)
{
    FakeAllocations allocations;
    FakeLender      lender;
    int             runtime = 0;
    allocations.refuse_ = true;
    CrossAdapterRows refused(allocations, lender);
    CHECK_EQ(refused.make(64u, 64u, b8g8r8a8, &runtime), kernel_refusal);
    CHECK_EQ(lender.lent_, 0u);
    CHECK_EQ(allocations.returned_, 0u);
    allocations.refuse_ = false;
    CrossAdapterRows no_format(allocations, lender);
    CHECK_EQ(no_format.make(64u, 64u, 0u, &runtime), av::d3d::hr_invalid_arg);
    CrossAdapterRows no_size(allocations, lender);
    CHECK_EQ(no_size.make(0u, 64u, b8g8r8a8, &runtime), av::d3d::hr_invalid_arg);
    CHECK_EQ(allocations.made_, 1u);
    CHECK_EQ(lender.lent_, 0u);
}

TEST(cross_adapter_rows, the_adapter_that_opens_the_allocation_lends_the_same_rows_at_the_makers_pitch)
{
    FakeAllocations  allocations;
    FakeLender       lender;
    int              runtime = 0;
    CrossAdapterRows made(allocations, lender);
    CHECK_EQ(made.make(160u, 120u, b8g8r8a8, &runtime), av::d3d::hr_ok);
    FakeAllocations  display_allocations;
    FakeLender       display_lender;
    CrossAdapterRows opened(display_allocations, display_lender);
    constexpr std::uint32_t opened_handle = 0x40000b80u;
    CHECK_EQ(opened.adopt(opened_handle, allocations.described_), av::d3d::hr_ok);
    CHECK_EQ(display_allocations.made_, 0u);
    CHECK_EQ(display_lender.lent_, 1u);
    CHECK_EQ(display_lender.allocation_, opened_handle);
    CHECK_EQ(display_lender.bytes_, lender.bytes_);
    CHECK_EQ(opened.pitch(), made.pitch());
    CHECK_EQ(opened.allocation(), opened_handle);
    CHECK(opened.lent() == FakeLender::pages());
}

TEST(cross_adapter_rows, an_allocation_of_one_adapter_only_is_not_adopted)
{
    FakeAllocations allocations;
    FakeLender      lender;
    KernelSurface   local(64u, 64u, b8g8r8a8);
    CHECK_STATUS(local.make(), Status::ok);
    AllocationDescriptor descriptor;
    CHECK_STATUS(descriptor.decode(local.allocation_data(), AllocationDescriptor::encoded_size), Status::ok);
    CrossAdapterRows rows(allocations, lender);
    CHECK_EQ(rows.adopt(handle, descriptor), av::d3d::hr_invalid_arg);
    CHECK_EQ(lender.lent_, 0u);
    lender.refuse_ = true;
    KernelSurface across(av::d3d::SharedWith::another_adapter, 64u, 64u, b8g8r8a8);
    CHECK_STATUS(across.make(), Status::ok);
    CHECK_STATUS(descriptor.decode(across.allocation_data(), AllocationDescriptor::encoded_size), Status::ok);
    CHECK_EQ(rows.adopt(0u, descriptor), av::d3d::hr_invalid_arg);
    CHECK_EQ(rows.adopt(handle, descriptor), av::d3d::hr_out_of_memory);
    CHECK(rows.lent() == nullptr);
}
