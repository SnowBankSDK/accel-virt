#include "executor/d3d/export_turn.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstdint>

namespace av {

struct ExportTurnProbe {
    static void set_turn(ExportTurn &turn, std::uint32_t value) { turn.turn_ = value; }
};

}

using av::ExportTurn;
using av::ExportTurnProbe;

TEST(export_turn, copies_alternate_between_the_two_images_as_each_is_handed_to_the_display)
{ ExportTurn turn;
  turn.place(0u, 4u);
  turn.place(1u, 9u);
  turn.begin();
  CHECK_EQ(turn.next(), 4u);
  turn.handed(4u);
  CHECK_EQ(turn.next(), 9u);
  turn.handed(9u);
  CHECK_EQ(turn.next(), 4u);
  CHECK(turn.invariants_hold()); }

TEST(export_turn, a_copy_that_was_not_handed_over_keeps_the_turn)
{ ExportTurn turn;
  turn.place(0u, 4u);
  turn.place(1u, 9u);
  turn.begin();
  turn.handed(9u);
  CHECK_EQ(turn.next(), 4u);
  turn.handed(4u);
  turn.handed(4u);
  CHECK_EQ(turn.next(), 9u); }

TEST(export_turn, the_display_names_an_image_by_its_turn_whichever_slot_holds_it)
{ ExportTurn turn;
  CHECK_EQ(turn.shown_as(0u), 0u);
  CHECK_EQ(turn.shown_as(1u), 1u);
  turn.place(0u, 6u);
  turn.place(1u, 2u);
  CHECK_EQ(turn.shown_as(6u), 0u);
  CHECK_EQ(turn.shown_as(2u), 1u);
  CHECK_EQ(turn.slot(0u), 6u);
  CHECK_EQ(turn.slot(1u), 2u); }

TEST(export_turn, a_new_pair_of_images_starts_at_the_first)
{ ExportTurn turn;
  turn.handed(turn.next());
  CHECK_EQ(turn.next(), 1u);
  turn.place(0u, 3u);
  turn.place(1u, 5u);
  turn.begin();
  CHECK_EQ(turn.next(), 3u); }

TEST(export_turn, an_image_past_the_pair_is_neither_placed_nor_read)
{ ExportTurn turn;
  turn.place(ExportTurn::images, 7u);
  CHECK_EQ(turn.slot(0u), 0u);
  CHECK_EQ(turn.slot(1u), 1u);
  CHECK_EQ(turn.slot(ExportTurn::images), 0u);
  CHECK(turn.invariants_hold()); }

TEST(export_turn, a_turn_past_the_pair_or_one_slot_for_both_images_breaks_the_invariant)
{ ExportTurn past;
  ExportTurnProbe::set_turn(past, ExportTurn::images);
  CHECK(!past.invariants_hold());
  ExportTurn shared;
  shared.place(1u, 0u);
  CHECK(!shared.invariants_hold()); }
