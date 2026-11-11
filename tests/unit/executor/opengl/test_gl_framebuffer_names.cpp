#include "executor/opengl/gl_error_queue.hpp"
#include "executor/opengl/gl_framebuffer_names.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/mock_platform.hpp"
#include "proto/generated/opengl/gl_dispatch_server.h"
#include "proto/generated/opengl/gl_protocol_ids.h"
#include "proto/generated/opengl/gl_real_dispatch.h"
#include "proto/submission/gl_error_word.hpp"
#include "proto/wire/wire_arena.hpp"
#include "proto/wire/wire_reader.hpp"
#include "proto/wire/wire_writer.hpp"

#include <cstdint>

using av::GlErrorQueue;
using av::GlErrorWord;
using av::GlFramebufferNames;
namespace ids = av::glproto;

namespace {

constexpr GLenum gl_framebuffer = 0x8D40u;
constexpr GLenum gl_draw_framebuffer_binding = 0x8CA6u;
constexpr GLenum gl_read_framebuffer_binding = 0x8CAAu;
constexpr GLenum gl_viewport = 0x0BA2u;
constexpr GLenum gl_framebuffer_complete = 0x8CD5u;
constexpr std::uint32_t invalid_value = 0x0501u;
constexpr std::uint32_t invalid_operation = 0x0502u;

GLuint        next_host = 0u;
GLuint        bound_host = 0u;
GLuint        asked_host = 0u;
GLuint        labelled_host = 0u;
std::uint32_t host_calls = 0u;
GLuint        deleted_hosts[256] = {};
std::uint32_t deleted_count = 0u;
std::uint32_t recorded = 0u;
bool          failing_gen = false;

void gen_framebuffers(GLsizei count, GLuint *names)
{
    host_calls++;
    for (GLsizei at = 0; at < count; at++) names[at] = failing_gen ? 0u : next_host++;
}

void bind_framebuffer(GLenum, GLuint name)
{
    host_calls++;
    bound_host = name;
}

GLboolean is_framebuffer(GLuint name)
{
    host_calls++;
    asked_host = name;
    return 1u;
}

GLenum check_status(GLuint name, GLenum)
{
    host_calls++;
    asked_host = name;
    return gl_framebuffer_complete;
}

void delete_framebuffers(GLsizei count, const GLuint *names)
{
    host_calls++;
    for (GLsizei at = 0; at < count && deleted_count < 256u; at++) deleted_hosts[deleted_count++] = names[at];
}

void get_integer(GLenum pname, GLint *data)
{
    data[0] = pname == gl_viewport ? 77 : static_cast<GLint>(bound_host);
}

void object_label(GLenum, GLuint name, GLsizei, const GLchar *)
{
    host_calls++;
    labelled_host = name;
}

void record_error(void *, std::uint32_t error)
{
    recorded = error;
}

struct Context {
    av::glproto::RealGl gl;
    GlErrorWord         word;
    GlErrorQueue        errors{gl, word};
    GlFramebufferNames  names;
    std::uint8_t        block[4096] = {};
    std::uint8_t        answer[256] = {};
    std::size_t         answered = 0u;

    explicit Context(av::MockPlatform &platform, bool compatibility = true) : names(platform, gl, errors)
    {
        gl.glGenFramebuffers = gen_framebuffers;
        gl.glCreateFramebuffers = gen_framebuffers;
        gl.glBindFramebuffer = bind_framebuffer;
        gl.glIsFramebuffer = is_framebuffer;
        gl.glCheckNamedFramebufferStatus = check_status;
        gl.glDeleteFramebuffers = delete_framebuffers;
        gl.glGetIntegerv = get_integer;
        gl.glObjectLabel = object_label;
        gl.av_record_error = record_error;
        gl.av_fixed_function = compatibility;
        names.install();
    }

    bool run(std::uint32_t command, const av::WireWriter &frame, const std::uint8_t *bytes)
    {
        av::WireArena  arena(block, sizeof block);
        av::WireReader payload(bytes, frame.offset());
        av::WireWriter reply(answer, sizeof answer);
        const bool     ran = ids::dispatch_command(gl, command, payload, reply, arena);
        answered = reply.offset();
        return ran;
    }

    GLuint generate()
    {
        std::uint8_t   frame[16];
        av::WireWriter writer(frame, sizeof frame);
        writer.write_varint<std::uint64_t>(1u);
        writer.write_pod<std::uint8_t>(1u);
        writer.write_varint<std::uint64_t>(sizeof(GLuint));
        CHECK(run(ids::kCmd_glGenFramebuffers, writer, frame));
        av::WireReader reply(answer, answered);
        std::uint64_t  bytes = 0u;
        GLuint         name = 0u;
        CHECK(reply.read_varint(bytes));
        CHECK_EQ(bytes, sizeof name);
        CHECK(reply.read(&name, sizeof name));
        return name;
    }

    bool bind(GLuint name)
    {
        std::uint8_t   frame[16];
        av::WireWriter writer(frame, sizeof frame);
        writer.write_varint<std::uint64_t>(gl_framebuffer);
        writer.write_varint<std::uint64_t>(name);
        return run(ids::kCmd_glBindFramebuffer, writer, frame);
    }

    std::uint64_t scalar_reply(std::uint32_t command, GLuint name, bool targeted)
    {
        std::uint8_t   frame[16];
        av::WireWriter writer(frame, sizeof frame);
        writer.write_varint<std::uint64_t>(name);
        if (targeted) writer.write_varint<std::uint64_t>(gl_framebuffer);
        CHECK(run(command, writer, frame));
        av::WireReader reply(answer, answered);
        std::uint64_t  value = 0u;
        CHECK(reply.read_varint(value));
        return value;
    }

    void remove(GLuint name)
    {
        std::uint8_t   frame[32];
        av::WireWriter writer(frame, sizeof frame);
        writer.write_varint<std::uint64_t>(1u);
        writer.write_pod<std::uint8_t>(1u);
        writer.write_varint<std::uint64_t>(sizeof name);
        writer.write(&name, sizeof name);
        CHECK(run(ids::kCmd_glDeleteFramebuffers, writer, frame));
    }

    GLint binding(GLenum pname)
    {
        std::uint8_t   frame[16];
        av::WireWriter writer(frame, sizeof frame);
        writer.write_varint<std::uint64_t>(pname);
        writer.write_pod<std::uint8_t>(1u);
        writer.write_varint<std::uint64_t>(sizeof(GLint));
        CHECK(run(ids::kCmd_glGetIntegerv, writer, frame));
        av::WireReader reply(answer, answered);
        std::uint64_t  bytes = 0u;
        GLint          value = -1;
        CHECK(reply.read_varint(bytes));
        CHECK(reply.read(&value, sizeof value));
        return value;
    }

    bool label(GLenum identifier, GLuint name)
    {
        std::uint8_t   frame[32];
        av::WireWriter writer(frame, sizeof frame);
        writer.write_varint<std::uint64_t>(identifier);
        writer.write_varint<std::uint64_t>(name);
        writer.write_varint<std::uint64_t>(1u);
        writer.write_pod<std::uint8_t>(1u);
        writer.write_varint<std::uint64_t>(1u);
        writer.write("x", 1u);
        return run(ids::kCmd_glObjectLabel, writer, frame);
    }
};

void reset_host()
{
    next_host = 41u;
    bound_host = 0u;
    asked_host = 0u;
    labelled_host = 0u;
    host_calls = 0u;
    deleted_count = 0u;
    recorded = 0u;
    failing_gen = false;
}

}

TEST(gl_framebuffer_names, two_contexts_of_one_share_group_see_different_framebuffers_under_one_name)
{ reset_host();
  av::MockPlatform platform;
  Context first(platform);
  Context second(platform);
  const GLuint made_first = first.generate();
  const GLuint made_second = second.generate();
  CHECK_EQ(made_first, 1u);
  CHECK_EQ(made_second, 1u);
  CHECK(second.bind(1u));
  CHECK_EQ(bound_host, 42u);
  CHECK_EQ(second.binding(gl_draw_framebuffer_binding), 1);
  CHECK(first.bind(1u));
  CHECK_EQ(bound_host, 41u);
  CHECK_EQ(first.binding(gl_read_framebuffer_binding), 1);
  CHECK_EQ(first.binding(gl_viewport), 77);
  CHECK(first.bind(0u));
  CHECK_EQ(bound_host, 0u);
  CHECK_EQ(first.binding(gl_draw_framebuffer_binding), 0);
  CHECK_EQ(second.generate(), 2u);
  CHECK_EQ(first.names.guest_of(43u), 0u);
  CHECK_EQ(second.names.guest_of(43u), 2u);
  CHECK_EQ(first.names.count(), 1u);
  CHECK_EQ(second.names.count(), 2u);
  CHECK_EQ(recorded, 0u);
  CHECK(first.names.invariants_hold());
  CHECK(second.names.invariants_hold()); }

TEST(gl_framebuffer_names, a_name_the_context_never_made_is_no_framebuffer_and_binds_one_only_in_compatibility)
{ reset_host();
  av::MockPlatform platform;
  Context core(platform, false);
  CHECK_EQ(core.scalar_reply(ids::kCmd_glIsFramebuffer, 9u, false), 0u);
  CHECK_EQ(recorded, 0u);
  CHECK_EQ(host_calls, 0u);
  CHECK_EQ(core.scalar_reply(ids::kCmd_glCheckNamedFramebufferStatus, 9u, true), 0u);
  CHECK_EQ(recorded, invalid_operation);
  CHECK_EQ(host_calls, 0u);
  recorded = 0u;
  CHECK(core.bind(9u));
  CHECK_EQ(recorded, invalid_operation);
  CHECK_EQ(host_calls, 0u);
  CHECK_EQ(core.names.count(), 0u);
  recorded = 0u;
  Context compatibility(platform);
  CHECK(compatibility.bind(9u));
  CHECK_EQ(recorded, 0u);
  CHECK_EQ(bound_host, 41u);
  CHECK_EQ(compatibility.binding(gl_draw_framebuffer_binding), 9);
  CHECK_EQ(compatibility.scalar_reply(ids::kCmd_glIsFramebuffer, 9u, false), 1u);
  CHECK_EQ(asked_host, 41u);
  CHECK_EQ(compatibility.scalar_reply(ids::kCmd_glCheckNamedFramebufferStatus, 9u, true), gl_framebuffer_complete);
  CHECK_EQ(asked_host, 41u);
  CHECK_EQ(compatibility.generate(), 1u);
  CHECK_EQ(compatibility.generate(), 2u);
  CHECK_EQ(compatibility.generate(), 3u);
  GLuint skipped = 0u;
  for (GLuint at = 0u; at < 6u; at++) skipped = compatibility.generate();
  CHECK_EQ(skipped, 10u);
  failing_gen = true;
  CHECK(compatibility.bind(50u));
  CHECK_EQ(recorded, invalid_operation);
  CHECK(compatibility.names.invariants_hold()); }

TEST(gl_framebuffer_names, a_deleted_name_is_given_back_on_the_host_and_forgotten)
{ reset_host();
  av::MockPlatform platform;
  Context context(platform);
  const GLuint made = context.generate();
  context.remove(made);
  CHECK_EQ(deleted_count, 1u);
  CHECK_EQ(deleted_hosts[0], 41u);
  CHECK_EQ(context.names.count(), 0u);
  CHECK_EQ(context.scalar_reply(ids::kCmd_glIsFramebuffer, made, false), 0u);
  context.remove(77u);
  context.remove(0u);
  CHECK_EQ(deleted_count, 1u);
  GLuint names[GlFramebufferNames::deleted_at_once + 3] = {};
  for (GLuint &name : names) name = context.generate();
  context.names.deleted(static_cast<GLsizei>(sizeof names / sizeof names[0]), names);
  CHECK_EQ(deleted_count, 1u + GlFramebufferNames::deleted_at_once + 3u);
  CHECK_EQ(context.names.count(), 0u);
  CHECK(context.names.invariants_hold()); }

TEST(gl_framebuffer_names, a_framebuffer_label_names_the_contexts_own_framebuffer)
{ reset_host();
  av::MockPlatform platform;
  Context context(platform);
  const GLuint made = context.generate();
  CHECK(context.label(gl_framebuffer, made));
  CHECK_EQ(labelled_host, 41u);
  CHECK(context.label(0x82E1u, 5u));
  CHECK_EQ(labelled_host, 5u);
  CHECK(context.label(gl_framebuffer, 5u));
  CHECK_EQ(recorded, invalid_value);
  CHECK_EQ(labelled_host, 5u); }

TEST(gl_framebuffer_names, a_name_with_no_room_to_keep_is_given_back_and_release_gives_back_every_one)
{ reset_host();
  av::MockPlatform platform;
  Context context(platform);
  GLuint names[3] = {50u, 0u, 51u};
  context.names.made(3, names);
  CHECK_EQ(names[0], 1u);
  CHECK_EQ(names[1], 0u);
  CHECK_EQ(names[2], 2u);
  GLuint starved[1] = {60u};
  for (std::uint32_t attempt = 0u; attempt < 64u && starved[0] != 0u; attempt++) {
    starved[0] = 60u + attempt;
    platform.fail_allocation_at(1u);
    context.names.made(1, starved);
  }
  platform.fail_allocation_at(0u);
  CHECK_EQ(starved[0], 0u);
  CHECK(context.word.count(0x0505u - GlErrorWord::first_error) != 0u);
  CHECK(deleted_count >= 1u);
  CHECK(context.names.invariants_hold());
  deleted_count = 0u;
  const std::uint32_t kept = context.names.count();
  context.names.release();
  CHECK_EQ(deleted_count, kept);
  CHECK_EQ(context.names.count(), 0u);
  GLuint again[1] = {70u};
  context.names.made(1, again);
  CHECK_EQ(again[0], 1u);
  context.names.forget_all();
  CHECK_EQ(context.names.count(), 0u);
  CHECK(context.names.invariants_hold()); }
