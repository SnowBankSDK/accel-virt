#include "executor/vulkan/dedicated_imports.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/failing_blocks.hpp"

#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using av::DedicatedImports;

namespace {

constexpr std::uint64_t memory = 0x4000u;
constexpr std::uint64_t other = 0x8000u;
constexpr std::uint64_t image = 0xc000u;
constexpr std::uint64_t bytes = 4194304u;

bool open_descriptor(int fd)
{ return fcntl(fd, F_GETFD) != -1; }

}

TEST(dedicated_imports, a_payload_is_its_own_copy_of_the_descriptor_until_dropped)
{ av::FailingBlocks blocks;
  DedicatedImports   imports(blocks);
  const int          fd = memfd_create("payload", 0);
  CHECK(fd >= 0);
  CHECK(imports.keep_payload(memory, fd, bytes) == av::Status::ok);
  close(fd);
  const int kept = imports.payload(memory);
  CHECK(kept >= 0);
  CHECK(open_descriptor(kept));
  CHECK_EQ(imports.payload_bytes(memory), bytes);
  CHECK_EQ(imports.payload(other), -1);
  CHECK_EQ(imports.payload_bytes(other), 0u);
  CHECK_EQ(imports.payloads(), 1u);
  CHECK(imports.invariants_hold());
  imports.drop_payload(memory);
  imports.drop_payload(memory);
  CHECK(!open_descriptor(kept));
  CHECK_EQ(imports.payload(memory), -1);
  CHECK_EQ(imports.payloads(), 0u); }

TEST(dedicated_imports, keeping_a_payload_again_replaces_the_first)
{ av::FailingBlocks blocks;
  DedicatedImports   imports(blocks);
  const int          first = memfd_create("first", 0);
  const int          second = memfd_create("second", 0);
  CHECK(imports.keep_payload(memory, first, bytes) == av::Status::ok);
  CHECK(imports.keep_payload(memory, second, 2u * bytes) == av::Status::ok);
  struct stat kept = {};
  struct stat wanted = {};
  CHECK_EQ(fstat(imports.payload(memory), &kept), 0);
  CHECK_EQ(fstat(second, &wanted), 0);
  CHECK(kept.st_ino == wanted.st_ino);
  CHECK_EQ(imports.payload_bytes(memory), 2u * bytes);
  CHECK_EQ(imports.payloads(), 1u);
  close(first);
  close(second); }

TEST(dedicated_imports, a_payload_without_a_descriptor_or_a_size_is_refused)
{ av::FailingBlocks blocks;
  DedicatedImports   imports(blocks);
  const int          fd = memfd_create("refused", 0);
  CHECK(imports.keep_payload(0u, fd, bytes) == av::Status::invalid_parameter);
  CHECK(imports.keep_payload(memory, -1, bytes) == av::Status::invalid_parameter);
  CHECK(imports.keep_payload(memory, fd, 0u) == av::Status::invalid_parameter);
  CHECK(imports.keep_payload(memory, 1000000, bytes) == av::Status::no_memory);
  CHECK_EQ(imports.payloads(), 0u);
  close(fd); }

TEST(dedicated_imports, an_image_holds_one_dedicated_import_until_it_is_taken)
{ av::FailingBlocks blocks;
  DedicatedImports   imports(blocks);
  CHECK(imports.dedicate(image, memory) == av::Status::ok);
  CHECK(imports.dedicate(image, other) == av::Status::invalid_parameter);
  CHECK(imports.dedicate(0u, other) == av::Status::invalid_parameter);
  CHECK(imports.dedicate(other, 0u) == av::Status::invalid_parameter);
  CHECK_EQ(imports.dedicated(), 1u);
  CHECK(imports.invariants_hold());
  CHECK_EQ(imports.take_dedicated(image), memory);
  CHECK_EQ(imports.take_dedicated(image), 0u);
  CHECK_EQ(imports.dedicated(), 0u); }

TEST(dedicated_imports, what_cannot_be_kept_is_refused)
{ av::FailingBlocks blocks;
  DedicatedImports   imports(blocks);
  const int          fd = memfd_create("full", 0);
  blocks.fail_from(blocks.requests() + 1u);
  CHECK(imports.keep_payload(memory, fd, bytes) != av::Status::ok);
  CHECK(imports.dedicate(image, memory) != av::Status::ok);
  CHECK_EQ(imports.payloads(), 0u);
  CHECK_EQ(imports.dedicated(), 0u);
  blocks.heal();
  CHECK(imports.keep_payload(memory, fd, bytes) == av::Status::ok);
  CHECK(imports.invariants_hold());
  close(fd); }
