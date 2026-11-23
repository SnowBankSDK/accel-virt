#include "core/log/driver_log.hpp"
#include "executor/dma_buf_write_fence.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/icd/recording_log.hpp"
#include "harness/memory/udmabuf.hpp"

#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

using av::DmaBufWriteFence;

namespace {

constexpr std::uint64_t buffer_bytes = 4096u;

}

TEST(dma_buf_write_fence, a_copy_fence_goes_onto_a_dma_buf_and_nothing_is_logged)
{ av::Udmabuf buffer(buffer_bytes);
  if (!buffer.made()) {
      std::fprintf(stderr, "no udmabuf device: the dma-buf fence is not run\n");
      return;
  }
  av::RecordingLog sink;
  av::DriverLog    lines(sink, "executor: ");
  const int copy = buffer.read_fence();
  CHECK(copy >= 0);
  DmaBufWriteFence fence(lines);
  CHECK(fence.attach(buffer.fd(), copy));
  CHECK(fence.attach(buffer.fd(), copy));
  CHECK_EQ(fence.refused(), 0u);
  CHECK_EQ(sink.lines(), 0u);
  close(copy); }

TEST(dma_buf_write_fence, a_descriptor_that_is_no_dma_buf_refuses_the_fence_and_says_so_once)
{ av::RecordingLog sink;
  av::DriverLog    lines(sink, "executor: ");
  const int memory = memfd_create("av-not-dma-buf", MFD_CLOEXEC);
  const int copy = memfd_create("av-not-sync-file", MFD_CLOEXEC);
  CHECK(memory >= 0);
  CHECK(copy >= 0);
  DmaBufWriteFence fence(lines);
  CHECK(!fence.attach(memory, copy));
  CHECK(!fence.attach(memory, copy));
  CHECK_EQ(fence.refused(), 2u);
  CHECK_EQ(sink.lines(), 1u);
  CHECK_EQ(sink.holding("each export waits for its copy"), 1u);
  close(copy);
  close(memory); }

TEST(dma_buf_write_fence, a_missing_image_or_copy_is_refused_without_asking_the_kernel)
{ av::RecordingLog sink;
  av::DriverLog    lines(sink, "executor: ");
  DmaBufWriteFence fence(lines);
  CHECK(!fence.attach(-1, 3));
  CHECK(!fence.attach(3, -1));
  CHECK_EQ(fence.refused(), 2u);
  CHECK_EQ(sink.lines(), 1u); }
