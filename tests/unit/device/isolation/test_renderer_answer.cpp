#include "core/support/status.hpp"
#include "device/isolation/renderer_answer.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/host/mock_host.hpp"
#include "proto/renderer/allocated_memory_message.hpp"
#include "proto/renderer/exported_image_message.hpp"
#include "proto/renderer/renderer_message_header.hpp"

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

using av::MockHost;
using av::RendererAnswer;
using av::RendererMessageHeader;
using av::Status;
using Kind = av::RendererMessageHeader::Kind;

namespace {

int fresh_descriptor()
{
    int ends[2] = {-1, -1};
    if (::pipe2(ends, O_CLOEXEC) != 0) return -1;
    ::close(ends[1]);
    return ends[0];
}

bool is_open(int descriptor) { return ::fcntl(descriptor, F_GETFD) != -1; }

}

TEST(renderer_answer, carries_its_kind_sequence_first_descriptor_and_decodes_its_body_with_what_came_with_it)
{ MockHost     host;
  std::uint8_t body[av::ExportedImageMessage::encoded_size] = {};
  CHECK_STATUS(av::ExportedImageMessage(true, 1u, 64u, 32u, 256u, 0u, 0x34325258u, 0u).encode(body, sizeof body),
               Status::ok);
  int descriptors[1] = {fresh_descriptor()};
  const RendererAnswer answer(host, RendererMessageHeader(Kind::exported_image, sizeof body, 41u), body, descriptors,
                              1u);
  CHECK(answer.kind() == Kind::exported_image);
  CHECK_EQ(answer.sequence(), 41u);
  CHECK_EQ(answer.descriptor(), descriptors[0]);
  av::ExportedImageMessage exported;
  CHECK_STATUS(answer.decode(exported), Status::ok);
  CHECK(exported.fresh());
  CHECK_EQ(exported.slot(), 1u);
  CHECK_EQ(exported.width(), 64u);
  av::AllocatedMemoryMessage allocated;
  CHECK_STATUS(answer.decode(allocated), Status::malformed);
  CHECK(is_open(descriptors[0]));
  ::close(descriptors[0]); }

TEST(renderer_answer, with_no_descriptor_has_none_to_give_and_a_body_that_needs_one_does_not_decode)
{ MockHost     host;
  std::uint8_t body[av::ExportedImageMessage::encoded_size] = {};
  CHECK_STATUS(av::ExportedImageMessage(true, 0u, 64u, 32u, 256u, 0u, 0x34325258u, 0u).encode(body, sizeof body),
               Status::ok);
  const RendererAnswer answer(host, RendererMessageHeader(Kind::exported_image, sizeof body, 7u), body, nullptr, 0u);
  CHECK_EQ(answer.descriptor(), -1);
  av::ExportedImageMessage exported;
  CHECK_STATUS(answer.decode(exported), Status::malformed);
  answer.close();
  CHECK_EQ(host.unclaimed_violations(), 0u); }

TEST(renderer_answer, close_closes_every_descriptor_that_came_with_it)
{ MockHost     host;
  std::uint8_t body[av::AllocatedMemoryMessage::encoded_size] = {};
  int          descriptors[2] = {fresh_descriptor(), fresh_descriptor()};
  const RendererAnswer answer(host, RendererMessageHeader(Kind::allocated_memory, sizeof body, 9u), body, descriptors,
                              2u);
  answer.close();
  CHECK(!is_open(descriptors[0]));
  CHECK(!is_open(descriptors[1])); }
