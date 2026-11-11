#include "harness/framework/test_registry.hpp"
#include "harness/transport/mock_transport.hpp"
#include "proto/generated/opencl/cl_default_dispatch.h"
#include "proto/generated/opencl/cl_dispatch_client_words.h"
#include "proto/generated/opencl/cl_dispatch_server.h"
#include "proto/generated/opencl/cl_protocol_ids.h"
#include "proto/wire/guest_bytes.hpp"
#include "proto/wire/wire_arena.hpp"
#include "proto/wire/wire_reader.hpp"
#include "proto/wire/wire_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

using av::MockTransport;
using av::WireReader;
using av::WireWriter;
using av::clproto::GuestInfoValue;
using av::clproto::GuestWordKind;
using av::clproto::HandleKind;

namespace {

constexpr std::size_t calls_bytes = 8192u;
constexpr std::uint64_t first_word = 0x0000000100000010ull;

class WireLog {
public:
    WireLog() : bytes_{}, used_(0u) {}

    void append(const MockTransport &transport)
    {
        const std::uint32_t command = transport.last_command();
        const std::uint64_t length = transport.last_length();
        if (used_ + sizeof command + sizeof length + length > sizeof bytes_) return;
        std::memcpy(bytes_ + used_, &command, sizeof command);
        used_ += sizeof command;
        std::memcpy(bytes_ + used_, &length, sizeof length);
        used_ += sizeof length;
        std::memcpy(bytes_ + used_, transport.last_payload(), static_cast<std::size_t>(length));
        used_ += static_cast<std::size_t>(length);
    }

    bool same_as(const WireLog &other) const
    {
        return used_ == other.used_ && std::memcmp(bytes_, other.bytes_, used_) == 0;
    }

    std::size_t used() const { return used_; }

private:
    std::uint8_t bytes_[calls_bytes];
    std::size_t  used_;
};

template <typename Word>
void encode_call_set(WireLog &log)
{
    MockTransport transport;
    std::uint8_t  reply[8] = {};
    transport.set_reply(reply, sizeof reply);
    Word sizes[3] = {};
    Word answered = 0u;
    char name[64] = {};
    static_cast<void>(av::clproto::av_clGetDeviceInfo<Word>(transport, 7u, CL_DEVICE_MAX_WORK_ITEM_SIZES,
                                                            sizeof sizes, sizes, &answered));
    log.append(transport);
    static_cast<void>(av::clproto::av_clGetDeviceInfo<Word>(transport, 7u, CL_DEVICE_MAX_WORK_GROUP_SIZE, 0u,
                                                            nullptr, &answered));
    log.append(transport);
    static_cast<void>(av::clproto::av_clGetDeviceInfo<Word>(transport, 7u, CL_DEVICE_NAME, sizeof name, name,
                                                            &answered));
    log.append(transport);
    static_cast<void>(av::clproto::av_clGetDeviceInfo<Word>(transport, 7u, CL_DEVICE_PARTITION_PROPERTIES,
                                                            sizeof sizes, sizes, nullptr));
    log.append(transport);
    static_cast<void>(av::clproto::av_clGetMemObjectInfo<Word>(transport, 9u, CL_MEM_SIZE, sizeof answered,
                                                               &answered, nullptr));
    log.append(transport);
    static_cast<void>(av::clproto::av_clGetKernelWorkGroupInfo<Word>(
        transport, 11u, 7u, CL_KERNEL_COMPILE_WORK_GROUP_SIZE, sizeof sizes, sizes, &answered));
    log.append(transport);

    const av::clproto::guest_cl_buffer_region<Word> region = {256u, 1024u};
    cl_int error = CL_SUCCESS;
    static_cast<void>(av::clproto::av_clCreateSubBuffer<Word>(transport, 9u, CL_MEM_READ_WRITE,
                                                              CL_BUFFER_CREATE_TYPE_REGION, &region, &error));
    log.append(transport);

    const cl_image_format format = {CL_RGBA, CL_UNORM_INT8};
    av::clproto::guest_cl_image_desc<Word> desc = {};
    desc.image_type = CL_MEM_OBJECT_IMAGE2D_ARRAY;
    desc.image_width = 640u;
    desc.image_height = 480u;
    desc.image_depth = 1u;
    desc.image_array_size = 6u;
    desc.image_row_pitch = 2560u;
    desc.image_slice_pitch = 2560u * 480u;
    desc.num_mip_levels = 0u;
    desc.num_samples = 0u;
    static_cast<void>(av::clproto::av_clCreateImage<Word>(transport, 5u, CL_MEM_READ_ONLY, &format, &desc,
                                                          0x51u, nullptr, 0u, &error));
    log.append(transport);

    const Word global[2] = {64u, 32u};
    const Word local[2] = {8u, 8u};
    static_cast<void>(av::clproto::av_clEnqueueNDRangeKernel<Word>(transport, 3u, 11u, 2u, nullptr, global, local,
                                                                   0u, nullptr, nullptr));
    log.append(transport);
    static_cast<void>(av::clproto::av_clGetKernelSubGroupInfo<Word>(
        transport, 11u, 7u, CL_KERNEL_MAX_SUB_GROUP_SIZE_FOR_NDRANGE, sizeof local, local, sizeof answered,
        &answered, nullptr));
    log.append(transport);

    const Word origin[3] = {4u, 2u, 0u};
    const Word extent[3] = {16u, 4u, 1u};
    std::uint8_t into[16u * 4u] = {};
    static_cast<void>(av::clproto::av_clEnqueueReadBufferRect<Word>(transport, 3u, 9u, CL_TRUE, origin, origin,
                                                                    extent, 64u, 0u, 32u, 0u, into, sizeof into,
                                                                    0u, nullptr, nullptr));
    log.append(transport);

    const char *strings[2] = {"kernel void a() {}", "kernel void b() {}"};
    const Word  lengths[2] = {0u, 10u};
    static_cast<void>(av::clproto::av_clCreateProgramWithSource<Word>(transport, 5u, 2u, strings, lengths, &error));
    log.append(transport);
}

class DescribedObjects final : public av::clproto::DefaultDispatch {
public:
    explicit DescribedObjects(const av::GuestBytes &guest)
        : DefaultDispatch(guest), desc_{}, region_{}, buffer_(nullptr), described_(0u)
    {
    }

    void *handle_for(HandleKind, std::uint64_t wire) const override
    {
        return reinterpret_cast<void *>(static_cast<std::uintptr_t>(wire));
    }
    void  made(HandleKind, void *) override {}
    bool  room_for(std::uint64_t) override { return true; }
    void  retained(HandleKind, void *) override {}
    void  released(HandleKind, void *) override {}
    void *svm_for(std::uint64_t, std::uint64_t) const override { return nullptr; }

    cl_mem on_clCreateImage(cl_context, cl_mem_flags, const cl_image_format *, const cl_image_desc *image_desc,
                            void *, cl_int *errcode_ret) override
    {
        desc_ = *image_desc;
        buffer_ = image_desc->buffer;
        described_++;
        if (errcode_ret != nullptr) *errcode_ret = CL_SUCCESS;
        return nullptr;
    }

    cl_mem on_clCreateSubBuffer(cl_mem, cl_mem_flags, cl_buffer_create_type, const cl_buffer_region *region,
                                cl_int *errcode_ret) override
    {
        region_ = *region;
        described_++;
        if (errcode_ret != nullptr) *errcode_ret = CL_SUCCESS;
        return nullptr;
    }

    const cl_image_desc    &desc() const { return desc_; }
    const cl_buffer_region &region() const { return region_; }
    cl_mem                  buffer() const { return buffer_; }
    std::uint32_t           described() const { return described_; }

protected:
    void  called(std::uint32_t) override {}
    void  handed(HandleKind, void *) override {}
    void  touched(const void *, std::size_t) override {}
    void *produce(HandleKind) override { return nullptr; }

private:
    cl_image_desc    desc_;
    cl_buffer_region region_;
    cl_mem           buffer_;
    std::uint32_t    described_;
};

class StructSession {
public:
    StructSession() : block_{}, arena_(block_, sizeof block_), guest_(), objects_(guest_), reply_{} {}

    bool decode(const MockTransport &transport)
    {
        arena_.reset();
        WireReader payload(transport.last_payload(), transport.last_length());
        WireWriter answer(reply_, sizeof reply_);
        const bool decoded = av::clproto::dispatch_command(objects_, transport.last_command(), payload, answer, arena_);
        return decoded && payload.remaining() == 0u;
    }

    const DescribedObjects &objects() const { return objects_; }

private:
    alignas(16) std::uint8_t block_[4096];
    av::WireArena    arena_;
    av::GuestBytes   guest_;
    DescribedObjects objects_;
    std::uint8_t     reply_[256];
};

template <typename Word>
std::size_t image_description_round_trip(StructSession &session)
{
    MockTransport transport;
    const cl_image_format format = {CL_R, CL_FLOAT};
    av::clproto::guest_cl_image_desc<Word> desc = {};
    desc.image_type = CL_MEM_OBJECT_IMAGE1D_BUFFER;
    desc.image_width = 4096u;
    desc.image_height = 3u;
    desc.image_depth = 2u;
    desc.image_array_size = 5u;
    desc.image_row_pitch = 16384u;
    desc.image_slice_pitch = 49152u;
    desc.num_mip_levels = 1u;
    desc.num_samples = 4u;
    cl_int error = CL_SUCCESS;
    static_cast<void>(av::clproto::av_clCreateImage<Word>(transport, 5u, CL_MEM_READ_ONLY, &format, &desc,
                                                          0x7700u, nullptr, 0u, &error));
    CHECK(session.decode(transport));
    return transport.last_length();
}

template <typename Word>
std::size_t buffer_region_round_trip(StructSession &session)
{
    MockTransport transport;
    const av::clproto::guest_cl_buffer_region<Word> region = {0xFFFF0000u, 0x10000u};
    cl_int error = CL_SUCCESS;
    static_cast<void>(av::clproto::av_clCreateSubBuffer<Word>(transport, 9u, CL_MEM_READ_WRITE,
                                                              CL_BUFFER_CREATE_TYPE_REGION, &region, &error));
    CHECK(session.decode(transport));
    return transport.last_length();
}

std::size_t host_words(std::uint8_t *reply, std::size_t room, cl_int result, const std::uint64_t *words,
                       std::size_t count, bool size_wanted)
{
    WireWriter answer(reply, room);
    answer.write_varint(static_cast<std::uint64_t>(static_cast<std::int64_t>(result)));
    answer.write_varint(static_cast<std::uint64_t>(count * sizeof(std::uint64_t)));
    for (std::size_t i = 0; i < count; ++i) answer.write_pod<std::uint64_t>(words[i]);
    if (size_wanted) answer.write_varint(static_cast<std::uint64_t>(count * sizeof(std::uint64_t)));
    return answer.offset();
}

std::uint64_t requested_host_bytes(const MockTransport &transport)
{
    WireReader request(transport.last_payload(), transport.last_length());
    std::uint64_t device = 0u;
    std::uint64_t name = 0u;
    std::uint64_t bytes = 0u;
    CHECK(request.read_pod(device));
    CHECK(request.read_varint(name));
    CHECK(request.read_varint(bytes));
    return bytes;
}

}

TEST(cl_guest_words, a_32_bit_guest_asks_the_host_for_eight_bytes_a_word)
{ const GuestInfoValue<std::uint32_t> one(GuestWordKind::unsigned_word, 4u);
  const GuestInfoValue<std::uint32_t> three(GuestWordKind::unsigned_word, 12u);
  const GuestInfoValue<std::uint32_t> short_of_one(GuestWordKind::unsigned_word, 3u);
  const GuestInfoValue<std::uint32_t> ragged(GuestWordKind::unsigned_word, 14u);
  const GuestInfoValue<std::uint32_t> text(GuestWordKind::none, 14u);
  CHECK_EQ(one.host_bytes(), 8u);
  CHECK_EQ(three.host_bytes(), 24u);
  CHECK_EQ(short_of_one.host_bytes(), 0u);
  CHECK_EQ(ragged.host_bytes(), 24u);
  CHECK_EQ(text.host_bytes(), 14u); }

TEST(cl_guest_words, host_words_narrow_into_guest_words_saturating)
{ std::uint8_t reply[64] = {};
  WireWriter answer(reply, sizeof reply);
  answer.write_pod<std::uint64_t>(1024u);
  answer.write_pod<std::uint64_t>(0x0000000100000000ull);
  answer.write_pod<std::uint64_t>(0xFFFFFFFFull);
  const GuestInfoValue<std::uint32_t> words(GuestWordKind::unsigned_word, 12u);
  std::uint32_t                       into[4] = {0xA5A5A5A5u, 0xA5A5A5A5u, 0xA5A5A5A5u, 0xA5A5A5A5u};
  WireReader                          from(reply, answer.offset());
  CHECK(words.read(from, 24u, into));
  CHECK_EQ(into[0], 1024u);
  CHECK_EQ(into[1], 0xFFFFFFFFu);
  CHECK_EQ(into[2], 0xFFFFFFFFu);
  CHECK_EQ(into[3], 0xA5A5A5A5u);
  CHECK_EQ(from.remaining(), 0u); }

TEST(cl_guest_words, a_signed_host_word_saturates_at_the_guest_s_signed_range)
{ std::uint8_t reply[64] = {};
  WireWriter answer(reply, sizeof reply);
  answer.write_pod<std::int64_t>(-5);
  answer.write_pod<std::int64_t>(std::int64_t{1} << 40);
  answer.write_pod<std::int64_t>(-(std::int64_t{1} << 40));
  answer.write_pod<std::int64_t>(0x1086);
  const GuestInfoValue<std::uint32_t> words(GuestWordKind::signed_word, 16u);
  std::uint32_t                       into[4] = {};
  WireReader                          from(reply, answer.offset());
  CHECK(words.read(from, 32u, into));
  CHECK_EQ(into[0], 0xFFFFFFFBu);
  CHECK_EQ(into[1], 0x7FFFFFFFu);
  CHECK_EQ(into[2], 0x80000000u);
  CHECK_EQ(into[3], 0x1086u); }

TEST(cl_guest_words, the_reported_size_is_scaled_to_guest_words)
{ const GuestInfoValue<std::uint32_t> words(GuestWordKind::unsigned_word, 0u);
  const GuestInfoValue<std::uint32_t> text(GuestWordKind::none, 0u);
  CHECK_EQ(words.guest_size(8u), 4u);
  CHECK_EQ(words.guest_size(24u), 12u);
  CHECK_EQ(text.guest_size(24u), 24u); }

TEST(cl_guest_words, a_reply_the_guest_did_not_make_room_for_or_not_in_whole_words_is_refused)
{ std::uint8_t reply[64] = {};
  const GuestInfoValue<std::uint32_t> words(GuestWordKind::unsigned_word, 8u);
  std::uint32_t                       into[4] = {};
  WireReader                          too_long(reply, sizeof reply);
  CHECK(!words.read(too_long, 24u, into));
  WireReader ragged(reply, sizeof reply);
  CHECK(!words.read(ragged, 12u, into));
  WireReader nothing(reply, 0u);
  CHECK(words.read(nothing, 0u, into)); }

TEST(cl_guest_words, a_64_bit_guest_takes_the_host_s_bytes_as_they_are)
{ std::uint8_t reply[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const GuestInfoValue<std::uint64_t> words(GuestWordKind::none, 16u);
  std::uint8_t                        into[16] = {};
  WireReader                          from(reply, sizeof reply);
  CHECK_EQ(words.host_bytes(), 16u);
  CHECK(words.read(from, 16u, into));
  CHECK(std::memcmp(into, reply, sizeof reply) == 0);
  CHECK_EQ(words.guest_size(24u), 24u);
  CHECK(av::clproto::word_of_clGetDeviceInfo<std::uint64_t>(CL_DEVICE_MAX_WORK_GROUP_SIZE) == GuestWordKind::none); }

TEST(cl_guest_words, every_word_sized_query_is_found_and_text_is_not)
{ CHECK(av::clproto::word_of_clGetDeviceInfo<std::uint32_t>(CL_DEVICE_MAX_WORK_ITEM_SIZES) ==
        GuestWordKind::unsigned_word);
  CHECK(av::clproto::word_of_clGetDeviceInfo<std::uint32_t>(CL_DEVICE_PARTITION_TYPE) == GuestWordKind::signed_word);
  CHECK(av::clproto::word_of_clGetDeviceInfo<std::uint32_t>(CL_DEVICE_NAME) == GuestWordKind::none);
  CHECK(av::clproto::word_of_clGetDeviceInfo<std::uint32_t>(CL_DEVICE_GLOBAL_MEM_SIZE) == GuestWordKind::none);
  CHECK(av::clproto::word_of_clGetMemObjectInfo<std::uint32_t>(CL_MEM_OFFSET) == GuestWordKind::unsigned_word);
  CHECK(av::clproto::word_of_clGetImageInfo<std::uint32_t>(CL_IMAGE_ROW_PITCH) == GuestWordKind::unsigned_word);
  CHECK(av::clproto::word_of_clGetProgramInfo<std::uint32_t>(CL_PROGRAM_BINARY_SIZES) ==
        GuestWordKind::unsigned_word);
  CHECK(av::clproto::word_of_clGetProgramInfo<std::uint32_t>(CL_PROGRAM_BINARIES) == GuestWordKind::none);
  CHECK(av::clproto::word_of_clGetPipeInfo<std::uint32_t>(CL_PIPE_PROPERTIES) == GuestWordKind::signed_word);
  CHECK(av::clproto::word_of_clGetKernelWorkGroupInfo<std::uint32_t>(CL_KERNEL_LOCAL_MEM_SIZE) ==
        GuestWordKind::none); }

TEST(cl_guest_words, a_32_bit_client_answers_three_work_item_sizes_in_twelve_bytes)
{ MockTransport transport;
  std::uint8_t  reply[64] = {};
  const std::uint64_t host[3] = {1024u, 512u, first_word};
  transport.set_reply(reply, host_words(reply, sizeof reply, CL_SUCCESS, host, 3u, true));
  std::uint32_t sizes[4] = {0u, 0u, 0u, 0xA5A5A5A5u};
  std::uint32_t answered = 0u;
  CHECK_EQ(av::clproto::av_clGetDeviceInfo<std::uint32_t>(transport, 7u, CL_DEVICE_MAX_WORK_ITEM_SIZES, 12u, sizes,
                                                          &answered),
           CL_SUCCESS);
  CHECK_EQ(requested_host_bytes(transport), 24u);
  CHECK_EQ(answered, 12u);
  CHECK_EQ(sizes[0], 1024u);
  CHECK_EQ(sizes[1], 512u);
  CHECK_EQ(sizes[2], 0xFFFFFFFFu);
  CHECK_EQ(sizes[3], 0xA5A5A5A5u); }

TEST(cl_guest_words, a_32_bit_client_answers_one_word_in_four_bytes)
{ MockTransport transport;
  std::uint8_t  reply[64] = {};
  const std::uint64_t host[1] = {256u};
  transport.set_reply(reply, host_words(reply, sizeof reply, CL_SUCCESS, host, 1u, true));
  std::uint32_t size = 0u;
  std::uint32_t answered = 0u;
  CHECK_EQ(av::clproto::av_clGetDeviceInfo<std::uint32_t>(transport, 7u, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof size,
                                                          &size, &answered),
           CL_SUCCESS);
  CHECK_EQ(requested_host_bytes(transport), 8u);
  CHECK_EQ(size, 256u);
  CHECK_EQ(answered, 4u); }

TEST(cl_guest_words, a_buffer_short_of_one_guest_word_is_asked_for_no_host_word_and_refused)
{ MockTransport transport;
  std::uint8_t  reply[16] = {};
  WireWriter    answer(reply, sizeof reply);
  answer.write_varint(static_cast<std::uint64_t>(static_cast<std::int64_t>(CL_INVALID_VALUE)));
  answer.write_varint(0u);
  transport.set_reply(reply, answer.offset());
  std::uint8_t size[3] = {0xA5u, 0xA5u, 0xA5u};
  CHECK_EQ(av::clproto::av_clGetDeviceInfo<std::uint32_t>(transport, 7u, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof size,
                                                          size, nullptr),
           CL_INVALID_VALUE);
  CHECK_EQ(requested_host_bytes(transport), 0u);
  CHECK_EQ(size[0], 0xA5u); }

TEST(cl_guest_words, the_size_of_a_word_query_alone_is_reported_in_guest_words)
{ MockTransport transport;
  std::uint8_t  reply[16] = {};
  WireWriter    answer(reply, sizeof reply);
  answer.write_varint(0u);
  answer.write_varint(24u);
  transport.set_reply(reply, answer.offset());
  std::uint32_t answered = 0u;
  CHECK_EQ(av::clproto::av_clGetDeviceInfo<std::uint32_t>(transport, 7u, CL_DEVICE_MAX_WORK_ITEM_SIZES, 0u, nullptr,
                                                          &answered),
           CL_SUCCESS);
  CHECK_EQ(answered, 12u); }

TEST(cl_guest_words, a_32_bit_and_a_64_bit_guest_put_the_same_bytes_on_the_wire)
{ WireLog narrow;
  WireLog wide;
  encode_call_set<std::uint32_t>(narrow);
  encode_call_set<std::size_t>(wide);
  CHECK(wide.used() > 0u);
  CHECK(narrow.same_as(wide)); }

TEST(cl_guest_words, an_image_description_crosses_whole_in_a_stream_its_width_does_not_change)
{ StructSession narrow;
  StructSession wide;
  const std::size_t narrow_bytes = image_description_round_trip<std::uint32_t>(narrow);
  const std::size_t wide_bytes = image_description_round_trip<std::size_t>(wide);
  CHECK_EQ(narrow_bytes, wide_bytes);
  const StructSession *const sessions[] = {&narrow, &wide};
  for (const StructSession *session : sessions) {
      const cl_image_desc &desc = session->objects().desc();
      CHECK_EQ(session->objects().described(), 1u);
      CHECK_EQ(desc.image_type, static_cast<cl_mem_object_type>(CL_MEM_OBJECT_IMAGE1D_BUFFER));
      CHECK_EQ(desc.image_width, 4096u);
      CHECK_EQ(desc.image_height, 3u);
      CHECK_EQ(desc.image_depth, 2u);
      CHECK_EQ(desc.image_array_size, 5u);
      CHECK_EQ(desc.image_row_pitch, 16384u);
      CHECK_EQ(desc.image_slice_pitch, 49152u);
      CHECK_EQ(desc.num_mip_levels, 1u);
      CHECK_EQ(desc.num_samples, 4u);
      CHECK(session->objects().buffer() == reinterpret_cast<cl_mem>(static_cast<std::uintptr_t>(0x7700u)));
  } }

TEST(cl_guest_words, a_buffer_region_crosses_whole_in_a_stream_its_width_does_not_change)
{ StructSession narrow;
  StructSession wide;
  const std::size_t narrow_bytes = buffer_region_round_trip<std::uint32_t>(narrow);
  const std::size_t wide_bytes = buffer_region_round_trip<std::size_t>(wide);
  CHECK_EQ(narrow_bytes, wide_bytes);
  const StructSession *const sessions[] = {&narrow, &wide};
  for (const StructSession *session : sessions) {
      CHECK_EQ(session->objects().described(), 1u);
      CHECK_EQ(session->objects().region().origin, 0xFFFF0000u);
      CHECK_EQ(session->objects().region().size, 0x10000u);
  } }
