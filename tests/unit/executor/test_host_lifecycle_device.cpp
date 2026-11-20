#include "core/log/driver_log.hpp"
#include "device/abi/av_executor.h"
#include "executor/egl_entry_gate.hpp"
#include "executor/host_lifecycle_gate.hpp"
#include "executor/host_lifecycle_gates.hpp"
#include "executor/shim/gl_native_fences.hpp"
#include "executor/opengl/opengl_backend.hpp"
#include "executor/shim/compositor_device.hpp"
#include "executor/shim/gl_display.hpp"
#include "harness/executor/widest_host_gpu.hpp"
#include "harness/framework/test_registry.hpp"
#include "harness/icd/recording_log.hpp"

#include <atomic>
#include <cstdint>
#include <unistd.h>
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

TEST(host_lifecycle_device, gl_batches_that_make_native_fences_and_a_vulkan_device_loop_on_one_gpu_both_finish_when_the_driver_is_one_that_excludes_egl)
{ av::WidestHostGpu gpu;
  if ((gpu.sub_apis() & AV_EXECUTOR_SUB_API_OPENGL) == 0u) return;
  constexpr std::uint32_t        rounds = 24u, batches = 400u;
  static av::HostLifecycleGates  gates;
  av::HostLifecycleGate *const   gate =
      gates.add(gpu.uuid(), av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "taken for the vendor's", "1"));
  CHECK(gate != nullptr);
  CHECK(gate != nullptr && gate->excludes_egl());
  CHECK(av::EglEntryGate::process().needed());
  static av::GlDisplay display;
  CHECK(gate != nullptr && display.open(gpu.uuid(), *gate));
  av::RecordingLog sink;
  av::DriverLog    lines(sink, "opengl: ");
  const std::uint64_t        excluded_before = av::EglEntryGate::process().exclusions();
  std::atomic<std::uint32_t> made{0u}, fenced{0u}, devices{0u};
  std::atomic<bool>          fences_here{true};
  std::thread                gl([&] {
      av::OpenGlBackend backend(lines);
      if (!backend.initialize(&display, nullptr, nullptr)) return;
      av::GlNativeFences fences;
      fences.configure(backend.context());
      fences_here = fences.available();
      for (std::uint32_t batch = 0u; batch < batches; batch++) {
          made++;
          const int fd = fences.make();
          if (fd >= 0) {
              fenced++;
              close(fd);
          }
      }
      fences.give_back();
      backend.shut_down();
  });
  std::thread vulkan([&] {
      for (std::uint32_t round = 0u; round < rounds; round++) {
          av::CompositorDevice device;
          static_cast<void>(device.open(gpu.uuid(), gate));
          device.close();
          devices++;
      }
  });
  gl.join();
  vulkan.join();
  CHECK_EQ(made.load(), batches);
  CHECK_EQ(devices.load(), rounds);
  if (fences_here.load()) CHECK_EQ(fenced.load(), batches);
  CHECK_EQ(gate->refusals(), 0u);
  CHECK(av::EglEntryGate::process().exclusions() - excluded_before >= std::uint64_t{rounds} * 2u);
  CHECK_EQ(av::EglEntryGate::process().inside(), 0u); }

TEST(host_lifecycle_device, gl_worker_threads_that_retire_and_exit_while_vulkan_devices_are_destroyed_on_a_driver_that_excludes_egl_all_finish)
{ av::WidestHostGpu gpu;
  if ((gpu.sub_apis() & AV_EXECUTOR_SUB_API_OPENGL) == 0u) return;
  constexpr std::uint32_t       rounds = 24u, workers = 24u;
  static av::HostLifecycleGates gates;
  av::HostLifecycleGate *const  gate =
      gates.add(gpu.uuid(), av::HostDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY, "taken for the vendor's", "1"));
  CHECK(gate != nullptr && gate->excludes_egl());
  static av::GlDisplay display;
  CHECK(gate != nullptr && display.open(gpu.uuid(), *gate));
  av::EglEntryGate          &egl = av::EglEntryGate::process();
  std::atomic<std::uint32_t> made{0u}, devices{0u};
  std::thread                vulkan([&] {
      for (std::uint32_t round = 0u; round < rounds; round++) {
          av::CompositorDevice device;
          static_cast<void>(device.open(gpu.uuid(), gate));
          device.close();
          devices++;
      }
  });
  for (std::uint32_t worker = 0u; worker < workers; worker++) {
      void       *kept = nullptr;
      std::thread gl([&] {
          av::RecordingLog sink;
          av::DriverLog    lines(sink, "opengl: ");
          {
              av::OpenGlBackend backend(lines);
              made += backend.initialize(&display, nullptr, nullptr) ? 1u : 0u;
              backend.shut_down();
          }
          kept = egl.retire();
      });
      gl.join();
      CHECK(kept != nullptr);
      egl.joined(kept);
  }
  vulkan.join();
  CHECK_EQ(made.load(), workers);
  CHECK_EQ(devices.load(), rounds);
  CHECK_EQ(gate->refusals(), 0u);
  CHECK_EQ(egl.inside(), 0u); }
