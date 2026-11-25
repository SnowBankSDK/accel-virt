#include "harness/framework/test_registry.hpp"
#include "harness/icd/lock_trace.hpp"
#include "harness/icd/recording_thread_lock.hpp"
#include "icd/common/calling_thread.hpp"
#include "icd/d3d/amortized_processing.hpp"
#include "icd/d3d/reentrant_call_lock.hpp"

#include <cstdint>

using av::d3d::AmortizedProcessing;
using av::d3d::ReentrantCallLock;

namespace {

constexpr std::uint64_t chunk = 262144u;

class Caller final : public av::icd::CallingThread {
public:
    std::uint32_t thread_id() override { return id_; }
    void          become(std::uint32_t id) { id_ = id; }

private:
    std::uint32_t id_ = 7u;
};

struct Rig {
    av::LockTrace           trace;
    av::RecordingThreadLock lock{trace, 'D'};
    Caller                  caller;
    ReentrantCallLock       calls{lock, caller};
    AmortizedProcessing     amortized{calls, caller};

    bool call_ending_at(std::uint64_t count, std::uint64_t step)
    {
        calls.acquire();
        const bool due = amortized.due(count, step);
        calls.release();
        return due;
    }
};

}

TEST(amortized_processing, a_deferred_context_is_due_once_per_256_kib_recorded)
{ Rig rig;
  CHECK(!rig.call_ending_at(0u, chunk));
  CHECK(!rig.call_ending_at(262143u, chunk));
  CHECK(rig.call_ending_at(262144u, chunk));
  CHECK(!rig.call_ending_at(262144u, chunk));
  CHECK(!rig.call_ending_at(524287u, chunk));
  CHECK(rig.call_ending_at(524288u, chunk));
  CHECK(!rig.call_ending_at(524288u, chunk));
  CHECK(!rig.lock.held()); }

TEST(amortized_processing, the_bytes_are_counted_from_the_last_call_not_from_a_256_kib_boundary)
{ Rig rig;
  CHECK(rig.call_ending_at(300000u, chunk));
  CHECK(!rig.call_ending_at(524288u, chunk));
  CHECK(!rig.call_ending_at(562143u, chunk));
  CHECK(rig.call_ending_at(562144u, chunk)); }

TEST(amortized_processing, a_call_that_records_more_than_256_kib_at_once_is_due_once)
{ Rig rig;
  CHECK(rig.call_ending_at(1048576u, chunk));
  CHECK(!rig.call_ending_at(1048576u, chunk));
  CHECK(!rig.call_ending_at(1310719u, chunk));
  CHECK(rig.call_ending_at(1310720u, chunk)); }

TEST(amortized_processing, the_immediate_context_is_due_after_each_call_that_sent_a_batch)
{ Rig rig;
  CHECK(!rig.call_ending_at(0u, 1u));
  CHECK(rig.call_ending_at(1u, 1u));
  CHECK(!rig.call_ending_at(1u, 1u));
  CHECK(rig.call_ending_at(3u, 1u)); }

TEST(amortized_processing, a_nested_call_is_never_due_and_leaves_its_bytes_to_the_outermost_call)
{ Rig rig;
  rig.calls.acquire();
  rig.calls.acquire();
  CHECK(!rig.amortized.due(262144u, chunk));
  rig.calls.release();
  CHECK(rig.amortized.due(262144u, chunk));
  rig.calls.release();
  CHECK(!rig.lock.held()); }

TEST(amortized_processing, a_call_from_inside_the_callback_is_never_due_and_its_bytes_stay_counted)
{ Rig rig;
  CHECK(rig.call_ending_at(262144u, chunk));
  rig.amortized.begin();
  CHECK(!rig.call_ending_at(524288u, chunk));
  CHECK(!rig.call_ending_at(1048576u, chunk));
  rig.amortized.end();
  CHECK(rig.call_ending_at(1048576u, chunk));
  CHECK(!rig.lock.held()); }

TEST(amortized_processing, another_thread_is_due_while_one_thread_is_inside_the_callback)
{ Rig rig;
  rig.amortized.begin();
  CHECK(!rig.call_ending_at(262144u, chunk));
  rig.caller.become(9u);
  CHECK(rig.call_ending_at(262144u, chunk));
  rig.caller.become(7u);
  rig.amortized.end();
  CHECK(!rig.call_ending_at(262144u, chunk)); }
