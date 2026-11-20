#include "core/log/log_cadence.hpp"
#include "harness/framework/test_registry.hpp"

#include <cstdint>

using av::LogCadence;

TEST(log_cadence, the_first_few_of_a_kind_are_spoken_and_then_one_in_each_stretch)
{ LogCadence cadence;
  std::uint32_t spoken = 0u;
  for (std::uint64_t seen = 1u; seen <= 1000u; seen++) {
      const bool speaks = cadence.speaks();
      CHECK_EQ(speaks, seen <= LogCadence::spoken_first || seen % LogCadence::then_every == 0u);
      spoken += speaks ? 1u : 0u;
  }
  CHECK_EQ(cadence.seen(), 1000u);
  CHECK_EQ(spoken, static_cast<std::uint32_t>(LogCadence::spoken_first + 1000u / LogCadence::then_every));
  cadence.reset();
  CHECK_EQ(cadence.seen(), 0u);
  CHECK(cadence.speaks()); }
