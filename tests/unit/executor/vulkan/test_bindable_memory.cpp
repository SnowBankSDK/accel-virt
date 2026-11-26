#include "executor/vulkan/bindable_memory.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/failing_blocks.hpp"

#include <cstdint>

using av::BindableMemory;

namespace {

constexpr std::uint64_t memory = 0x4000u;
constexpr std::uint64_t other = 0x8000u;
constexpr std::uint64_t page = 4096u;

}

TEST(bindable_memory, a_bind_fits_only_inside_the_memory_it_names)
{ av::FailingBlocks blocks;
  BindableMemory     sizes(blocks);
  CHECK(sizes.note(memory, 3u * page) == av::Status::ok);
  CHECK(sizes.invariants_hold());
  CHECK(sizes.fits(memory, 0u, 3u * page));
  CHECK(sizes.fits(memory, page, 2u * page));
  CHECK(sizes.fits(memory, 3u * page, 0u));
  CHECK(!sizes.fits(memory, 0u, 3u * page + 1u));
  CHECK(!sizes.fits(memory, page, 2u * page + 1u));
  CHECK(!sizes.fits(memory, 3u * page + 1u, 0u));
  CHECK(!sizes.fits(other, 0u, 1u)); }

TEST(bindable_memory, an_offset_and_size_that_wrap_around_do_not_fit)
{ av::FailingBlocks blocks;
  BindableMemory     sizes(blocks);
  CHECK(sizes.note(memory, 2u * page) == av::Status::ok);
  CHECK(!sizes.fits(memory, page, UINT64_MAX));
  CHECK(!sizes.fits(memory, UINT64_MAX, 2u)); }

TEST(bindable_memory, a_swapchain_slot_imported_at_its_packed_size_does_not_hold_its_padded_rows)
{ av::FailingBlocks blocks;
  BindableMemory     sizes(blocks);
  const std::uint64_t packed = (910u * 4u * 796u + page - 1u) / page * page;
  const std::uint64_t padded = 3840u * 796u;
  CHECK(sizes.note(memory, packed) == av::Status::ok);
  CHECK(!sizes.fits(memory, 0u, padded));
  CHECK(sizes.note(memory, padded) == av::Status::ok);
  CHECK(sizes.fits(memory, 0u, padded)); }

TEST(bindable_memory, memory_that_is_forgotten_or_never_sized_holds_nothing)
{ av::FailingBlocks blocks;
  BindableMemory     sizes(blocks);
  CHECK(sizes.note(memory, page) == av::Status::ok);
  CHECK(sizes.note(other, 2u * page) == av::Status::ok);
  CHECK(sizes.count() == 2u);
  sizes.forget(memory);
  sizes.forget(memory);
  CHECK(sizes.count() == 1u);
  CHECK(!sizes.fits(memory, 0u, 1u));
  CHECK(sizes.fits(other, 0u, 2u * page));
  CHECK(sizes.note(0u, page) == av::Status::invalid_parameter);
  CHECK(sizes.note(memory, 0u) == av::Status::invalid_parameter);
  CHECK(sizes.count() == 1u);
  CHECK(sizes.invariants_hold()); }

TEST(bindable_memory, a_size_that_cannot_be_kept_is_refused)
{ av::FailingBlocks blocks;
  BindableMemory     sizes(blocks);
  blocks.fail_from(blocks.requests() + 1u);
  CHECK(sizes.note(memory, page) != av::Status::ok);
  CHECK(!sizes.fits(memory, 0u, 1u));
  blocks.heal();
  CHECK(sizes.note(memory, page) == av::Status::ok);
  CHECK(sizes.fits(memory, 0u, page));
  CHECK(sizes.invariants_hold()); }
