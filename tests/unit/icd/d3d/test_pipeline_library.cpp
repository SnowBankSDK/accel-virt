#include "harness/framework/test_registry.hpp"
#include "harness/platform/failing_blocks.hpp"
#include "icd/d3d/hresult.hpp"
#include "icd/d3d/pipeline_library.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

using av::FailingBlocks;
using av::d3d::PipelineLibrary;

namespace {

std::vector<std::uint8_t> blob_of(std::uint32_t seed, std::size_t size)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t at = 0u; at < size; at++) bytes[at] = static_cast<std::uint8_t>(seed * 31u + at * 7u);
    return bytes;
}

std::vector<std::uint8_t> serialized(const PipelineLibrary &library)
{
    std::vector<std::uint8_t> bytes(library.serialized_size(), 0xEEu);
    library.serialize(bytes.data());
    return bytes;
}

bool holds(const PipelineLibrary &library, std::uint32_t index, const std::vector<std::uint8_t> &expected)
{
    const std::uint8_t *blob = nullptr;
    std::uint64_t       size = 0u;
    return library.find(index, blob, size) && size == expected.size() && std::memcmp(blob, expected.data(), size) == 0;
}

std::vector<std::uint8_t> three_pipelines()
{
    FailingBlocks   blocks;
    PipelineLibrary library(blocks);
    CHECK_EQ(library.add(4u, blob_of(4u, 13u).data(), 13u), av::d3d::hr_ok);
    CHECK_EQ(library.add(0u, blob_of(0u, 8u).data(), 8u), av::d3d::hr_ok);
    CHECK_EQ(library.add(70000u, blob_of(9u, 1031u).data(), 1031u), av::d3d::hr_ok);
    return serialized(library);
}

}

TEST(pipeline_library, an_empty_library_saves_a_header_that_opens_as_an_empty_library)
{ FailingBlocks   blocks;
  PipelineLibrary library(blocks);
  const std::vector<std::uint8_t> saved = serialized(library);
  CHECK_EQ(saved.size(), PipelineLibrary::header_bytes);
  PipelineLibrary reopened(blocks);
  CHECK_EQ(reopened.open(saved.data(), saved.size()), av::d3d::hr_ok);
  CHECK_EQ(reopened.entries(), 0u);
  CHECK(reopened.invariants_hold()); }

TEST(pipeline_library, nothing_given_opens_an_empty_library)
{ FailingBlocks   blocks;
  PipelineLibrary library(blocks);
  CHECK_EQ(library.open(nullptr, 0u), av::d3d::hr_ok);
  CHECK_EQ(library.entries(), 0u); }

TEST(pipeline_library, every_pipeline_saved_comes_back_under_its_index_with_its_bytes)
{ FailingBlocks blocks;
  const std::vector<std::uint8_t> saved = three_pipelines();
  CHECK_EQ(saved.size() % 8u, 0u);
  PipelineLibrary reopened(blocks);
  CHECK_EQ(reopened.open(saved.data(), saved.size()), av::d3d::hr_ok);
  CHECK_EQ(reopened.entries(), 3u);
  CHECK(holds(reopened, 4u, blob_of(4u, 13u)));
  CHECK(holds(reopened, 0u, blob_of(0u, 8u)));
  CHECK(holds(reopened, 70000u, blob_of(9u, 1031u)));
  CHECK(serialized(reopened) == saved);
  CHECK(reopened.invariants_hold()); }

TEST(pipeline_library, an_index_added_again_keeps_only_the_newer_pipeline)
{ FailingBlocks blocks;
  {
      PipelineLibrary library(blocks);
      CHECK_EQ(library.add(2u, blob_of(1u, 40u).data(), 40u), av::d3d::hr_ok);
      CHECK_EQ(library.add(2u, blob_of(2u, 24u).data(), 24u), av::d3d::hr_ok);
      CHECK_EQ(library.entries(), 1u);
      CHECK(holds(library, 2u, blob_of(2u, 24u)));
  }
  CHECK_EQ(blocks.live(), 0u); }

TEST(pipeline_library, an_index_never_added_is_not_found)
{ FailingBlocks   blocks;
  PipelineLibrary library(blocks);
  CHECK_EQ(library.add(1u, blob_of(1u, 16u).data(), 16u), av::d3d::hr_ok);
  const std::uint8_t *blob = nullptr;
  std::uint64_t       size = 7u;
  CHECK(!library.find(2u, blob, size));
  CHECK(blob == nullptr);
  CHECK_EQ(size, 7u); }

TEST(pipeline_library, an_empty_or_missing_or_oversized_blob_is_refused)
{ FailingBlocks   blocks;
  PipelineLibrary library(blocks);
  const std::uint8_t one = 1u;
  CHECK_EQ(library.add(0u, nullptr, 4u), av::d3d::hr_invalid_arg);
  CHECK_EQ(library.add(0u, &one, 0u), av::d3d::hr_invalid_arg);
  CHECK_EQ(library.add(0u, &one, PipelineLibrary::most_blob_bytes + 1u), av::d3d::hr_invalid_arg);
  CHECK_EQ(library.entries(), 0u);
  CHECK_EQ(blocks.live(), 0u); }

TEST(pipeline_library, a_library_saved_by_the_driver_before_opens_as_empty)
{ FailingBlocks   blocks;
  PipelineLibrary library(blocks);
  const std::uint8_t first[8] = {0x41, 0x56, 0x50, 0x4C, 1, 0, 0, 0};
  CHECK_EQ(library.open(first, sizeof first), av::d3d::hr_ok);
  CHECK_EQ(library.entries(), 0u);
  const std::uint8_t longer[12] = {0x41, 0x56, 0x50, 0x4C, 1, 0, 0, 0, 0, 0, 0, 0};
  CHECK_EQ(library.open(longer, sizeof longer), av::d3d::hr_d3d12_driver_version_mismatch); }

TEST(pipeline_library, a_library_of_another_driver_or_version_is_a_version_mismatch)
{ FailingBlocks blocks;
  std::vector<std::uint8_t> saved = three_pipelines();
  PipelineLibrary library(blocks);
  std::vector<std::uint8_t> foreign = saved;
  foreign[0] ^= 0xFFu;
  CHECK_EQ(library.open(foreign.data(), foreign.size()), av::d3d::hr_d3d12_driver_version_mismatch);
  std::vector<std::uint8_t> newer = saved;
  newer[4] = 3u;
  CHECK_EQ(library.open(newer.data(), newer.size()), av::d3d::hr_d3d12_driver_version_mismatch);
  CHECK_EQ(library.entries(), 0u);
  CHECK_EQ(blocks.live(), 0u); }

TEST(pipeline_library, every_cut_short_library_is_a_version_mismatch_and_keeps_nothing)
{ FailingBlocks blocks;
  const std::vector<std::uint8_t> saved = three_pipelines();
  PipelineLibrary library(blocks);
  for (std::size_t length = 1u; length < saved.size(); length++) {
      CHECK_EQ(library.open(saved.data(), length), av::d3d::hr_d3d12_driver_version_mismatch);
      CHECK_EQ(library.entries(), 0u);
      CHECK_EQ(blocks.live(), 0u);
  } }

TEST(pipeline_library, a_library_with_bytes_after_its_last_pipeline_is_a_version_mismatch)
{ FailingBlocks blocks;
  std::vector<std::uint8_t> saved = three_pipelines();
  saved.resize(saved.size() + 8u, 0u);
  PipelineLibrary library(blocks);
  CHECK_EQ(library.open(saved.data(), saved.size()), av::d3d::hr_d3d12_driver_version_mismatch);
  CHECK_EQ(blocks.live(), 0u); }

TEST(pipeline_library, a_library_naming_an_index_twice_or_an_empty_pipeline_is_a_version_mismatch)
{ FailingBlocks blocks;
  PipelineLibrary writer(blocks);
  CHECK_EQ(writer.add(5u, blob_of(5u, 8u).data(), 8u), av::d3d::hr_ok);
  CHECK_EQ(writer.add(6u, blob_of(6u, 8u).data(), 8u), av::d3d::hr_ok);
  std::vector<std::uint8_t> twice = serialized(writer);
  const std::size_t second = PipelineLibrary::header_bytes + PipelineLibrary::entry_header_bytes + 8u;
  twice[second] = 5u;
  PipelineLibrary library(blocks);
  CHECK_EQ(library.open(twice.data(), twice.size()), av::d3d::hr_d3d12_driver_version_mismatch);
  std::vector<std::uint8_t> empty = serialized(writer);
  for (std::size_t at = 0u; at < 8u; at++) empty[PipelineLibrary::header_bytes + 8u + at] = 0u;
  CHECK_EQ(library.open(empty.data(), empty.size()), av::d3d::hr_d3d12_driver_version_mismatch);
  CHECK_EQ(library.entries(), 0u); }

TEST(pipeline_library, a_library_claiming_more_pipelines_than_any_holds_is_a_version_mismatch)
{ FailingBlocks blocks;
  std::vector<std::uint8_t> saved = three_pipelines();
  const std::uint32_t count = PipelineLibrary::most_entries + 1u;
  std::memcpy(saved.data() + 8u, &count, 4u);
  PipelineLibrary library(blocks);
  CHECK_EQ(library.open(saved.data(), saved.size()), av::d3d::hr_d3d12_driver_version_mismatch); }

TEST(pipeline_library, any_byte_flipped_opens_or_is_refused_and_never_leaks_or_breaks_the_library)
{ FailingBlocks blocks;
  const std::vector<std::uint8_t> saved = three_pipelines();
  for (std::size_t at = 0u; at < saved.size(); at++) {
      std::vector<std::uint8_t> flipped = saved;
      flipped[at] ^= 0x5Au;
      {
          PipelineLibrary library(blocks);
          const av::d3d::Hresult opened = library.open(flipped.data(), flipped.size());
          CHECK(opened == av::d3d::hr_ok || opened == av::d3d::hr_d3d12_driver_version_mismatch);
          if (opened != av::d3d::hr_ok) CHECK_EQ(library.entries(), 0u);
          CHECK(library.invariants_hold());
      }
      CHECK_EQ(blocks.live(), 0u);
  } }

TEST(pipeline_library, memory_running_out_while_adding_keeps_what_was_there_and_leaks_nothing)
{ FailingBlocks blocks;
  {
      PipelineLibrary library(blocks);
      CHECK_EQ(library.add(1u, blob_of(1u, 16u).data(), 16u), av::d3d::hr_ok);
      blocks.fail_from(blocks.requests() + 1u);
      CHECK_EQ(library.add(2u, blob_of(2u, 16u).data(), 16u), av::d3d::hr_out_of_memory);
      blocks.heal();
      CHECK_EQ(library.entries(), 1u);
      CHECK(holds(library, 1u, blob_of(1u, 16u)));
      CHECK(library.invariants_hold());
  }
  CHECK_EQ(blocks.live(), 0u); }

TEST(pipeline_library, a_library_growing_past_its_first_room_keeps_every_pipeline)
{ FailingBlocks blocks;
  {
      PipelineLibrary library(blocks);
      for (std::uint32_t index = 0u; index < 100u; index++)
          CHECK_EQ(library.add(index, blob_of(index, 8u + index).data(), 8u + index), av::d3d::hr_ok);
      CHECK_EQ(library.entries(), 100u);
      for (std::uint32_t index = 0u; index < 100u; index++) CHECK(holds(library, index, blob_of(index, 8u + index)));
      CHECK(library.invariants_hold());
  }
  CHECK_EQ(blocks.live(), 0u); }
