#include "executor/d3d/d3d_refused_creates.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/failing_blocks.hpp"

#include <cstdint>

using av::D3dRefusedCreates;
using av::FailingBlocks;

TEST(d3d_refused_creates, a_noted_id_is_held_with_its_answer_until_forgotten)
{ FailingBlocks blocks;
  D3dRefusedCreates refused(blocks);
  CHECK(refused.note(7u, 0x8007000Eu));
  CHECK(refused.holds(7u));
  CHECK_EQ(refused.hresult_of(7u), 0x8007000Eu);
  CHECK(!refused.holds(8u));
  CHECK(refused.forget(7u));
  CHECK(!refused.holds(7u));
  CHECK(!refused.forget(7u));
  CHECK_EQ(refused.count(), 0u);
  CHECK(refused.invariants_hold()); }

TEST(d3d_refused_creates, an_id_is_noted_once_and_every_refusal_outstanding_is_held_however_many)
{ FailingBlocks blocks;
  D3dRefusedCreates refused(blocks);
  CHECK(refused.note(1u, 1u));
  CHECK(!refused.note(1u, 2u));
  CHECK_EQ(refused.hresult_of(1u), 1u);
  constexpr std::uint32_t outstanding = 4096u;
  for (std::uint32_t id = 2u; id <= outstanding; id++) CHECK(refused.note(id, id));
  CHECK_EQ(refused.count(), outstanding);
  CHECK(refused.holds(1u) && refused.holds(outstanding / 2u) && refused.holds(outstanding));
  CHECK_EQ(refused.hresult_of(outstanding - 1u), outstanding - 1u);
  for (std::uint32_t id = 1u; id <= outstanding; id++) CHECK(refused.forget(id));
  CHECK_EQ(refused.count(), 0u);
  CHECK(refused.invariants_hold());
  CHECK(refused.note(5u, 5u));
  refused.clear();
  CHECK_EQ(refused.count(), 0u); }

TEST(d3d_refused_creates, a_refusal_that_cannot_be_recorded_is_answered_false_and_changes_nothing)
{ FailingBlocks blocks;
  D3dRefusedCreates refused(blocks);
  blocks.fail_from(1u);
  CHECK(!refused.note(3u, 3u));
  CHECK(!refused.holds(3u));
  CHECK_EQ(refused.count(), 0u);
  CHECK(refused.invariants_hold()); }
