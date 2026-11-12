#include "core/log/driver_log.hpp"
#include "executor/eventfd_wake.hpp"
#include "executor/opengl/gl_batch_progress.hpp"
#include "executor/opengl/gl_buffer_slices.hpp"
#include "executor/opengl/gl_error_queue.hpp"
#include "executor/opengl/gl_upload_ring.hpp"
#include "executor/opengl/gl_memory_objects.hpp"
#include "executor/shim/gl_host_import.hpp"
#include "executor/shim/gl_native_fences.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/icd/recording_log.hpp"
#include "harness/platform/mock_platform.hpp"
#include "proto/generated/glmem/glmem_protocol_ids.h"
#include "proto/generated/opengl/gl_real_dispatch.h"
#include "proto/submission/gl_error_word.hpp"
#include "proto/wire/guest_bytes.hpp"

#include <cstdint>

using av::GlBatchProgress;
using av::GlBufferSlices;
using av::GlErrorQueue;
using av::GlErrorWord;
using av::GlHostImport;
using av::GlMemoryObjects;

namespace {

constexpr std::uint32_t invalid_operation = 0x0502u;
constexpr GLenum        array_buffer = 0x8892u;
constexpr GLenum        array_buffer_binding = 0x8894u;
constexpr GLenum        already_signalled = 0x911Au;
constexpr GLenum        timeout_expired = 0x911Bu;

std::uintptr_t next_sync = 0x1000u;
std::uint32_t  signalled_through = 0u;
std::uint32_t  driver_error = 0u;
std::uint32_t  error_on_store = 0u;
std::uint32_t  stored = 0u;
GLuint         stored_buffer = 0u;
GLsizeiptr     stored_size = 0;
GLuint         stored_memory = 0u;
GLuint64       stored_offset = 0u;
GLint          bound_buffer = 0;
GLenum         asked_binding = 0u;

GLsync make_sync(GLenum, GLbitfield)
{
    next_sync += 0x10u;
    return reinterpret_cast<GLsync>(next_sync);
}

GLenum wait_sync(GLsync sync, GLbitfield, GLuint64)
{
    return reinterpret_cast<std::uintptr_t>(sync) <= 0x1000u + 0x10u * signalled_through ? already_signalled
                                                                                       : timeout_expired;
}

void delete_sync(GLsync) {}

GLenum read_error()
{
    const std::uint32_t error = driver_error;
    driver_error = 0u;
    return error;
}

void storage_mem(GLuint buffer, GLsizeiptr size, GLuint memory, GLuint64 offset)
{
    stored++;
    stored_buffer = buffer;
    stored_size = size;
    stored_memory = memory;
    stored_offset = offset;
    driver_error = error_on_store;
}

void integer(GLenum pname, GLint *value)
{
    asked_binding = pname;
    *value = bound_buffer;
}

struct Rig {
    av::MockPlatform       platform;
    av::RecordingLog       sink;
    av::DriverLog          lines{sink, "gl: "};
    av::glproto::RealGl    gl;
    GlErrorWord            word;
    GlErrorQueue           errors{gl, word};
    GlHostImport           import;
    av::GlNativeFences     natives;
    av::EventfdWake        wake;
    GlBatchProgress        progress{gl, natives, wake};
    av::GuestBytes         guest;
    GlMemoryObjects        memories{platform, progress};
    GlBufferSlices         slices{memories, gl, import, progress, errors, guest, lines};

    Rig()
    {
        next_sync = 0x1000u;
        signalled_through = 0u;
        driver_error = 0u;
        error_on_store = 0u;
        stored = 0u;
        bound_buffer = 0;
        asked_binding = 0u;
        gl.glFenceSync = make_sync;
        gl.glClientWaitSync = wait_sync;
        gl.glDeleteSync = delete_sync;
        gl.glGetError = read_error;
        gl.glNamedBufferStorageMemEXT = storage_mem;
        gl.glGetIntegerv = integer;
    }
};

}

TEST(gl_buffer_slices, installing_hooks_the_four_calls_it_judges_ahead_of_the_driver)
{ Rig rig;
  CHECK(rig.gl.av_hook_glBufferStorageMemEXT == nullptr);
  rig.slices.install();
  CHECK(rig.gl.av_hook_user == &rig.slices);
  CHECK(rig.gl.av_hook_glBufferStorageMemEXT != nullptr);
  CHECK(rig.gl.av_hook_glNamedBufferStorageMemEXT != nullptr);
  CHECK(rig.gl.av_hook_glDeleteMemoryObjectsEXT != nullptr);
  CHECK(rig.gl.av_hook_glDeleteBuffers != nullptr);
  CHECK(rig.slices.knows(av::glmemproto::kCmd_PlaceBufferSlice));
  CHECK(rig.slices.knows(av::glmemproto::kCmd_ForgetBufferSlice));
  CHECK(!rig.slices.knows(0x10001u));
  CHECK(rig.memories.invariants_hold()); }

TEST(gl_buffer_slices, a_host_that_cannot_import_buffers_is_not_offered_to_place_any)
{ Rig rig;
  std::uint32_t supported = 7u;
  CHECK(rig.slices.on_QueryBufferSlicesGL(supported));
  CHECK_EQ(supported, 0u);
  CHECK(rig.progress.fences());
  CHECK(!rig.import.places_buffers());
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  std::uint32_t placed = 7u;
  CHECK(rig.slices.on_PlaceBufferSlice(3u, 7u, 0u, 0x1000u, placed));
  CHECK_EQ(placed, 0u);
  CHECK_EQ(rig.memories.placed(), 0u);
  CHECK_EQ(stored, 0u); }

TEST(gl_buffer_slices, a_named_buffer_stored_in_a_memory_is_placed_and_the_driver_is_told_the_same)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  CHECK_EQ(rig.memories.count(), 1u);
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0x1000, 7u, 0x2000u));
  CHECK_EQ(stored, 1u);
  CHECK_EQ(stored_buffer, 3u);
  CHECK_EQ(stored_size, 0x1000);
  CHECK_EQ(stored_memory, 7u);
  CHECK_EQ(stored_offset, 0x2000u);
  CHECK_EQ(rig.memories.placed(), 1u);
  CHECK_EQ(rig.word.count(1u), 0u);
  CHECK(rig.memories.invariants_hold()); }

TEST(gl_buffer_slices, a_store_outside_the_memory_or_of_nothing_or_into_unknown_memory_is_an_invalid_value)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0x1000, 7u, 0x4000u));
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0, 7u, 0u));
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, -4, 7u, 0u));
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0x1000, 9u, 0u));
  CHECK_EQ(rig.word.count(1u), 4u);
  CHECK_EQ(stored, 0u);
  CHECK_EQ(rig.memories.placed(), 0u);
  CHECK(rig.memories.invariants_hold()); }

TEST(gl_buffer_slices, an_error_the_driver_raises_storing_a_buffer_is_recorded_and_places_nothing)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  error_on_store = invalid_operation;
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0x1000, 7u, 0u));
  CHECK_EQ(rig.word.count(2u), 1u);
  CHECK_EQ(rig.memories.placed(), 0u);
  CHECK(rig.memories.invariants_hold()); }

TEST(gl_buffer_slices, a_store_into_the_buffer_bound_to_a_target_goes_to_that_buffer)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  bound_buffer = 5;
  CHECK(!rig.slices.on_buffer_storage_mem(array_buffer, 0x800, 7u, 0u));
  CHECK_EQ(asked_binding, array_buffer_binding);
  CHECK_EQ(stored_buffer, 5u);
  CHECK_EQ(rig.memories.placed(), 1u);
  stored = 0u;
  bound_buffer = 0;
  CHECK(rig.slices.on_buffer_storage_mem(array_buffer, 0x800, 7u, 0u));
  CHECK(rig.slices.on_buffer_storage_mem(0x1234u, 0x800, 7u, 0u));
  CHECK_EQ(stored, 0u);
  CHECK_EQ(rig.memories.placed(), 1u); }

TEST(gl_buffer_slices, a_buffer_deleted_or_forgotten_leaves_its_memory_and_the_driver_still_deletes_it)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0x1000, 7u, 0u));
  CHECK(!rig.slices.on_named_buffer_storage_mem(4u, 0x1000, 7u, 0x1000u));
  CHECK_EQ(rig.memories.placed(), 2u);
  const GLuint gone[] = {3u, 99u};
  CHECK(rig.slices.on_delete_buffers(2, gone));
  CHECK_EQ(rig.memories.placed(), 1u);
  CHECK(rig.slices.on_delete_buffers(0, gone));
  CHECK(rig.slices.on_delete_buffers(2, nullptr));
  CHECK_EQ(rig.memories.placed(), 1u);
  CHECK(rig.slices.on_ForgetBufferSlice(4u));
  CHECK_EQ(rig.memories.placed(), 0u);
  CHECK(rig.slices.on_ForgetBufferSlice(4u));
  CHECK(rig.memories.invariants_hold()); }

TEST(gl_buffer_slices, a_memory_deleted_is_kept_until_nothing_is_placed_in_it_and_the_gpu_has_passed_its_last_batch)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  CHECK(!rig.slices.on_named_buffer_storage_mem(3u, 0x1000, 7u, 0u));
  const GLuint memories[] = {7u};
  CHECK(!rig.slices.on_delete_memory_objects(1, memories));
  CHECK_EQ(rig.memories.count(), 1u);
  const GLuint buffers[] = {3u};
  CHECK(rig.slices.on_delete_buffers(1, buffers));
  rig.memories.reclaim(rig.import);
  CHECK_EQ(rig.memories.count(), 1u);
  rig.progress.fence();
  signalled_through = 1u;
  CHECK(!rig.progress.settle());
  rig.memories.reclaim(rig.import);
  CHECK_EQ(rig.memories.count(), 0u);
  CHECK(rig.memories.invariants_hold()); }

TEST(gl_buffer_slices, retiring_a_memory_by_name_and_deleting_none_change_what_is_held_only_when_asked)
{ Rig rig;
  CHECK_EQ(rig.memories.add(7u, 0x4000u), av::Status::ok);
  CHECK(!rig.slices.on_delete_memory_objects(1, nullptr));
  CHECK(!rig.slices.on_delete_memory_objects(0, nullptr));
  CHECK_EQ(rig.memories.count(), 1u);
  rig.memories.retire(7u);
  rig.progress.fence();
  signalled_through = 1u;
  CHECK(!rig.progress.settle());
  rig.memories.reclaim(rig.import);
  CHECK_EQ(rig.memories.count(), 0u);
  CHECK_EQ(rig.memories.add(8u, 0x1000u), av::Status::ok);
  rig.memories.destroy_all(rig.import);
  CHECK_EQ(rig.memories.count(), 0u);
  CHECK_EQ(rig.memories.placed(), 0u); }

namespace {

GLuint        made_buffer = 0u;
GLuint        filled_buffer = 0u;
std::uint8_t  filled[16] = {};
GLsizeiptr    filled_size = 0;
GLbitfield    filled_flags = 0xFFu;
GLuint        copied_from = 0u;
GLuint        copied_into = 0u;
GLintptr      copied_at = 0;
GLsizeiptr    copied_size = 0;
GLuint        given_back = 0u;
std::uint32_t write_calls = 0u;

void create_buffers(GLsizei, GLuint *names)
{
    write_calls++;
    names[0] = made_buffer;
}

void buffer_storage(GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags)
{
    write_calls++;
    filled_buffer = buffer;
    filled_size = size;
    filled_flags = flags;
    if (size <= static_cast<GLsizeiptr>(sizeof filled)) __builtin_memcpy(filled, data, static_cast<std::size_t>(size));
}

void copy_buffer(GLuint from, GLuint into, GLintptr, GLintptr at, GLsizeiptr size)
{
    write_calls++;
    copied_from = from;
    copied_into = into;
    copied_at = at;
    copied_size = size;
}

void delete_buffers(GLsizei, const GLuint *names)
{
    write_calls++;
    given_back = names[0];
}

}

TEST(gl_buffer_slices, a_slice_write_goes_through_a_staging_copy_the_gpu_runs_in_command_order)
{ Rig rig;
  write_calls = 0u;
  made_buffer = 77u;
  CHECK(rig.slices.knows(av::glmemproto::kCmd_WriteBufferSlice));
  const std::uint8_t data[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
  CHECK(rig.slices.on_WriteBufferSlice(5u, 16u, data, sizeof data));
  CHECK_EQ(write_calls, 0u);
  CHECK_EQ(rig.word.count(invalid_operation - GlErrorWord::first_error), 1u);
  rig.gl.glCreateBuffers = create_buffers;
  rig.gl.glNamedBufferStorage = buffer_storage;
  rig.gl.glCopyNamedBufferSubData = copy_buffer;
  rig.gl.glDeleteBuffers = delete_buffers;
  CHECK(rig.slices.on_WriteBufferSlice(5u, 16u, data, sizeof data));
  CHECK_EQ(write_calls, 4u);
  CHECK_EQ(filled_buffer, 77u);
  CHECK_EQ(filled_size, static_cast<GLsizeiptr>(sizeof data));
  CHECK_EQ(filled_flags, 0u);
  CHECK(__builtin_memcmp(filled, data, sizeof data) == 0);
  CHECK_EQ(copied_from, 77u);
  CHECK_EQ(copied_into, 5u);
  CHECK_EQ(copied_at, 16);
  CHECK_EQ(copied_size, static_cast<GLsizeiptr>(sizeof data));
  CHECK_EQ(given_back, 77u);
  CHECK(rig.slices.on_WriteBufferSlice(5u, 0u, data, 0u));
  CHECK_EQ(write_calls, 4u);
  CHECK(rig.slices.on_WriteBufferSlice(5u, ~std::uint64_t{0}, data, sizeof data));
  CHECK_EQ(write_calls, 4u);
  CHECK_EQ(rig.word.count(0x0501u - GlErrorWord::first_error), 1u);
  made_buffer = 0u;
  CHECK(rig.slices.on_WriteBufferSlice(5u, 0u, data, sizeof data));
  CHECK_EQ(write_calls, 5u);
  CHECK_EQ(rig.word.count(0x0505u - GlErrorWord::first_error), 1u); }

namespace {

struct RingCalls {
    std::uint32_t made = 0u;
    std::uint32_t deleted = 0u;
    GLuint        deleted_name = 0u;
    GLsizeiptr    ring_size = 0;
    GLbitfield    ring_flags = 0u;
    std::uint32_t filled = 0u;
    GLintptr      filled_at = -1;
    std::uint32_t copies = 0u;
    GLuint        copy_from = 0u;
    GLintptr      copy_read = -1;
    GLuint        copy_into = 0u;
    GLintptr      copy_at = -1;
    GLuint        next_name = 90u;
};

RingCalls ring_calls;

void ring_create(GLsizei, GLuint *names)
{
    ring_calls.made++;
    names[0] = ++ring_calls.next_name;
}

void ring_storage(GLuint, GLsizeiptr size, const void *, GLbitfield flags)
{
    ring_calls.ring_size = size;
    ring_calls.ring_flags = flags;
}

void ring_fill(GLuint, GLintptr at, GLsizeiptr, const void *)
{
    ring_calls.filled++;
    ring_calls.filled_at = at;
}

void ring_copy(GLuint from, GLuint into, GLintptr read, GLintptr at, GLsizeiptr)
{
    ring_calls.copies++;
    ring_calls.copy_from = from;
    ring_calls.copy_read = read;
    ring_calls.copy_into = into;
    ring_calls.copy_at = at;
}

void ring_delete(GLsizei, const GLuint *names)
{
    ring_calls.deleted++;
    ring_calls.deleted_name = names[0];
}

}

TEST(gl_buffer_slices, slice_writes_share_one_staging_ring_and_a_region_comes_back_only_after_its_fence_passes)
{ Rig rig;
  ring_calls = RingCalls{};
  rig.gl.glCreateBuffers = ring_create;
  rig.gl.glNamedBufferStorage = ring_storage;
  rig.gl.glNamedBufferSubData = ring_fill;
  rig.gl.glCopyNamedBufferSubData = ring_copy;
  rig.gl.glDeleteBuffers = ring_delete;
  static std::uint8_t data[av::GlUploadRing::largest] = {};
  CHECK(rig.slices.on_WriteBufferSlice(5u, 16u, data, 8u));
  CHECK_EQ(ring_calls.made, 1u);
  const GLuint ring = ring_calls.next_name;
  CHECK_EQ(ring_calls.ring_size, static_cast<GLsizeiptr>(av::GlUploadRing::ring_bytes));
  CHECK_EQ(ring_calls.ring_flags, 0x0100u);
  CHECK_EQ(ring_calls.filled_at, 0);
  CHECK_EQ(ring_calls.copy_from, ring);
  CHECK_EQ(ring_calls.copy_read, 0);
  CHECK_EQ(ring_calls.copy_into, 5u);
  CHECK_EQ(ring_calls.copy_at, 16);
  CHECK(rig.slices.on_WriteBufferSlice(6u, 0u, data, 300u));
  CHECK_EQ(ring_calls.made, 1u);
  CHECK_EQ(ring_calls.filled_at, static_cast<GLintptr>(av::GlUploadRing::alignment));
  for (std::uint32_t piece = 0u; piece < 3u; piece++)
      CHECK(rig.slices.on_WriteBufferSlice(5u, 0u, data, sizeof data));
  CHECK_EQ(ring_calls.made, 1u);
  CHECK_EQ(ring_calls.copies, 5u);
  CHECK_EQ(ring_calls.deleted, 0u);
  CHECK(rig.slices.on_WriteBufferSlice(5u, 0u, data, sizeof data));
  CHECK_EQ(ring_calls.made, 2u);
  CHECK_EQ(ring_calls.deleted, 1u);
  CHECK(ring_calls.deleted_name != ring);
  rig.progress.fence();
  CHECK(rig.slices.on_WriteBufferSlice(5u, 0u, data, sizeof data));
  CHECK_EQ(ring_calls.made, 3u);
  signalled_through = 1u;
  CHECK(!rig.progress.settle());
  CHECK(rig.slices.on_WriteBufferSlice(7u, 32u, data, 8u));
  CHECK_EQ(ring_calls.made, 3u);
  CHECK_EQ(ring_calls.copy_from, ring);
  CHECK_EQ(ring_calls.filled_at, 0);
  CHECK_EQ(ring_calls.copy_into, 7u);
  rig.slices.release();
  CHECK_EQ(ring_calls.deleted, 3u);
  CHECK_EQ(ring_calls.deleted_name, ring); }
