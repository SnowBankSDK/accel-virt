#include "miniport/scheduling/stamped_line.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/platform/mock_platform.hpp"

#include <cstdint>
#include <cstring>

using av::MockPlatform;
using av::StampedLine;

TEST(stamped_line, names_the_adapter_and_the_time_after_the_event_and_then_each_field)
{ MockPlatform platform;
  platform.advance_time(1234u * StampedLine::ticks_per_millisecond - platform.interrupt_time());
  int owner = 0;
  StampedLine line(platform, 0x7801u, "avgpu: a packet was let go");
  line.number("packets", 3u).address("owner", &owner).number("age", 1200u).code("status", 0xC000000Du);
  CHECK_EQ(platform.logged(), 0u);
  line.write();
  CHECK_EQ(platform.logged(), 1u);
  CHECK(std::strstr(platform.last_log(), "avgpu: a packet was let go: adapter 0000000000007801 at 1234 ms packets 3 owner ") ==
        platform.last_log());
  CHECK(std::strstr(platform.last_log(), " age 1200 status 0xC000000D") != nullptr); }

TEST(stamped_line, a_line_longer_than_its_room_is_cut_and_still_written)
{ MockPlatform platform;
  StampedLine  line(platform, 1u, "avgpu: a long event");
  for (std::uint32_t index = 0u; index < 40u; index++) line.number("field", index);
  line.write();
  CHECK_EQ(platform.logged(), 1u);
  CHECK(std::strlen(platform.last_log()) < 192u); }
