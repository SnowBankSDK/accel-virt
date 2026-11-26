#include "executor/vulkan/dma_buf_origin.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

using av::DmaBufOrigin;

namespace {

constexpr std::uint8_t gpu[DmaBufOrigin::uuid_bytes] = {0x7b, 0x7a, 0xd2, 0xc5, 0x42, 0x19, 0x8c, 0x80,
                                                        0xc4, 0xfd, 0xa6, 0x4b, 0x87, 0x75, 0xae, 0xdb};
constexpr std::uint8_t other[DmaBufOrigin::uuid_bytes] = {0x86, 0x80, 0xa6, 0x46, 0x0c, 0x00, 0x00, 0x00,
                                                          0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

}

TEST(dma_buf_origin, a_dma_buf_named_for_this_gpu_is_ours)
{ DmaBufOrigin origin;
  origin.learn(gpu);
  CHECK(origin.names_us("size:\t65536\ncount:\t1\nexp_name:\tdrm\nname:\tav-7b7ad2c542198c80c4fda64b\n"));
  CHECK(origin.names_us("name:   av-7b7ad2c542198c80c4fda64b")); }

TEST(dma_buf_origin, a_dma_buf_named_for_another_gpu_or_not_named_is_not_ours)
{ DmaBufOrigin origin;
  origin.learn(gpu);
  DmaBufOrigin elsewhere;
  elsewhere.learn(other);
  CHECK(!origin.names_us("exp_name:\ti915\nname:\tav-8680a6460c00000000020000\n"));
  CHECK(elsewhere.names_us("exp_name:\ti915\nname:\tav-8680a6460c00000000020000\n"));
  CHECK(!origin.names_us("size:\t65536\nexp_name:\tdrm\n"));
  CHECK(!origin.names_us("name:\tav-7b7ad2c542198c80c4fda64b77\n"));
  CHECK(!origin.names_us("name:\tav-7b7ad2c542198c80c4fda6\n"));
  CHECK(!origin.names_us("exp_name:\tav-7b7ad2c542198c80c4fda64b\n"));
  CHECK(!origin.names_us(""));
  CHECK(!origin.names_us(nullptr)); }

TEST(dma_buf_origin, nothing_is_ours_or_stamped_before_the_gpu_is_learned_or_after_it_is_forgotten)
{ DmaBufOrigin origin;
  CHECK(!origin.names_us("name:\tav-7b7ad2c542198c80c4fda64b\n"));
  CHECK(!origin.stamp(0));
  origin.learn(gpu);
  origin.forget();
  CHECK(!origin.names_us("name:\tav-7b7ad2c542198c80c4fda64b\n")); }

TEST(dma_buf_origin, a_descriptor_that_is_not_a_dma_buf_is_neither_stamped_nor_ours)
{ DmaBufOrigin origin;
  origin.learn(gpu);
  const int fd = memfd_create("not-a-dma-buf", 0);
  CHECK(fd >= 0);
  CHECK(!origin.stamp(fd));
  CHECK(!origin.ours(fd));
  CHECK(!origin.ours(-1));
  close(fd); }
