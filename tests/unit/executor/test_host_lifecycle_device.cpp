#include "core/log/driver_log.hpp"
#include "device/abi/av_executor.h"
#include "executor/host_lifecycle_gate.hpp"
#include "executor/opengl/opengl_backend.hpp"
#include "executor/shim/compositor_device.hpp"
#include "executor/shim/gl_display.hpp"
#include "harness/executor/widest_host_gpu.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/icd/recording_log.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

TEST(host_lifecycle_device, a_vulkan_device_loop_and_a_gl_context_loop_on_one_gpu_both_finish_with_every_call_inside_its_gate)
{ av::WidestHostGpu gpu;
  if ((gpu.sub_apis() & AV_EXECUTOR_SUB_API_OPENGL) == 0u) return;
  constexpr std::uint32_t   rounds = 24u;
  static av::HostLifecycleGate gate;
  static av::GlDisplay         display;
  CHECK(display.open(gpu.uuid(), gate));
  av::RecordingLog sink;
  av::DriverLog    lines(sink, "opengl: ");
  const auto gl_round = [&] {
      av::OpenGlBackend backend(lines);
      const bool        made = backend.initialize(&display, nullptr, nullptr);
      backend.shut_down();
      return made;
  };
  const std::uint64_t before_alone = gate.entries();
  CHECK(gl_round());
  const std::uint64_t per_gl_round = gate.entries() - before_alone;
  CHECK(per_gl_round >= 2u);
  const std::uint64_t        before = gate.entries();
  std::atomic<std::uint32_t> contexts{0u};
  std::atomic<std::uint32_t> devices{0u};
  std::thread                gl([&] {
      for (std::uint32_t round = 0u; round < rounds; round++) contexts += gl_round() ? 1u : 0u;
  });
  std::thread vulkan([&] {
      for (std::uint32_t round = 0u; round < rounds; round++) {
          av::CompositorDevice device;
          static_cast<void>(device.open(gpu.uuid(), &gate));
          device.close();
          devices++;
      }
  });
  gl.join();
  vulkan.join();
  CHECK_EQ(contexts.load(), rounds);
  CHECK_EQ(devices.load(), rounds);
  CHECK_EQ(gate.refusals(), 0u);
  CHECK_EQ(gate.entries() - before, std::uint64_t{rounds} * (per_gl_round + 2u)); }
