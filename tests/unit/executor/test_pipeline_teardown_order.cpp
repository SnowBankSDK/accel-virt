#include "executor/pipeline_teardown_order.hpp"
#include "harness/framework/test_registry.hpp"

#include <atomic>
#include <cstdint>
#include <chrono>
#include <thread>

using av::PipelineTeardownOrder;

TEST(pipeline_teardown_order, only_the_driver_whose_ray_tracing_compiles_deadlock_device_destruction_is_ordered)
{ PipelineTeardownOrder order;
  CHECK(order.needed_by(VK_DRIVER_ID_NVIDIA_PROPRIETARY));
  CHECK(!order.needed_by(VK_DRIVER_ID_MESA_RADV));
  CHECK(!order.needed_by(VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA));
  CHECK(!order.needed_by(VK_DRIVER_ID_MESA_NVK));
  CHECK(!order.needed_by(VK_DRIVER_ID_AMD_PROPRIETARY));
  CHECK(!order.needed_by(static_cast<VkDriverId>(0))); }

TEST(pipeline_teardown_order, a_destroy_waits_for_an_in_flight_ray_tracing_compile)
{ PipelineTeardownOrder order;
  order.compiling();
  std::atomic<bool> destroyed{false};
  std::thread destroyer([&] {
      order.destroying();
      destroyed = true;
      order.destroyed();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!destroyed.load());
  CHECK_EQ(order.destroys(), 0u);
  order.compiled();
  destroyer.join();
  CHECK(destroyed.load());
  CHECK_EQ(order.destroys(), 1u); }

TEST(pipeline_teardown_order, compiles_run_side_by_side_and_one_waits_for_a_destroy_in_progress)
{ PipelineTeardownOrder order;
  order.compiling();
  order.compiling();
  order.compiled();
  order.compiled();
  order.destroying();
  std::atomic<bool> compiled{false};
  std::thread compiler([&] {
      order.compiling();
      compiled = true;
      order.compiled();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!compiled.load());
  order.destroyed();
  compiler.join();
  CHECK(compiled.load()); }

TEST(pipeline_teardown_order, the_embedder_table_answers_and_orders_as_the_order_itself_does)
{ PipelineTeardownOrder order;
  const AvTeardownOrder &table = order.abi();
  CHECK_EQ(table.size, static_cast<std::uint32_t>(sizeof(AvTeardownOrder)));
  CHECK_EQ(table.needed_by(table.self, VK_DRIVER_ID_NVIDIA_PROPRIETARY), 1);
  CHECK_EQ(table.needed_by(table.self, VK_DRIVER_ID_MESA_RADV), 0);
  table.compiling(table.self);
  std::atomic<bool> destroyed{false};
  std::thread destroyer([&] {
      table.destroying(table.self);
      destroyed = true;
      table.destroyed(table.self);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!destroyed.load());
  table.compiled(table.self);
  destroyer.join();
  CHECK(destroyed.load());
  CHECK_EQ(order.destroys(), 1u); }
