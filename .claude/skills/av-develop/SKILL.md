---
name: av-develop
description: Build and test accel-virt's components (avgpu.sys, the guest UMDs and ICDs for Vulkan, Direct3D, OpenGL and OpenCL, the QEMU device and libav_executor.so, codegen, the Direct3D runtime) with the av-dev MCP server - build profiles, ctest areas, the source gates, scope marks and regenerating src/proto/generated. Use for any edit-build-test loop in accel-virt.
---

# Develop accel-virt

The `av-dev` MCP server (`tools/mcp/av_dev.py`) runs the steps. Builds and tests are background jobs: `wait_job`
waits up to 55 s and returns the log tail, `job_log` greps it, `cancel_job` stops it. Builds take the build lock,
run at four CPUs and refuse to start beside another build. Read `CLAUDE.md` and `docs/constitution.md` before
writing code: the gates enforce them.

## Build profiles

| Profile | Tree | What it is |
|---|---|---|
| default | `build/` | unit and harness tests, plus the guest cross-builds: `avgpu.sys` (`build/driver/`), the x64 and x86 UMDs and ICDs (`build/guest{,32}/`) |
| qemu | `build-qemu/` | QEMU with `virtio-gpu-av-pci`, `libav_executor.so`, `av-renderer`, `avrecord` (Release) |
| coverage | `build-coverage/` | the 100% branch coverage gate |
| fuzz | `build-fuzz/` | the libFuzzer targets |
| stack | `build-stack/` | Release executor without checks or sanitizers |

`build profile=<p>` configures the tree when it is missing (clang++, Ninja, the gate's options) and builds `all`, or
`target`: `drivers` for the guest package, `avgpu`, `avtest`, or any CMake target. `status` shows which trees are
configured.

## Tests

`test area=<a>`, optionally with `filter` (ctest -R):

- **unit**: every unit and harness test of `build/`, the fast loop.
- **tools**: the python tool tests (`tests/tools`).
- **gates**: design rules, symbol tests, scope marks, generated sources, self-containment and the policy checks.
- **images**: the PE import and export rules of every guest binary.
- **gpu**: the host full-stack tests (`*_device`, `test_fullstack_*`) that run the guest ICDs against the real
  executor on this machine's GPU. Without a usable GPU they pass and print "nothing judged": read the log before
  calling it a pass. They need the Direct3D runtime (`d3d_runtime`).
- **fuzz**: the smoke fuzzers in `build-fuzz` (`fuzz_seconds` per target).
- **coverage**: the coverage tree's tests and its gate.
- **qemu**: the qtest suites of `build-qemu`.

`check name=<design|symbols|scope_marks|generated|self_contained>` runs one gate on the sources right now, without a
build. `apply_scope_marks` writes missing `AV_SCOPE` marks.

## Generated code

`src/proto/generated/` is never edited by hand. Change the generator in `tools/codegen/` or its profile in
`tools/codegen/profiles/`, then `regenerate` (all APIs, or `apis`), then `check name=generated`. The Khronos
registries come from `tools/fetch_khronos_registries.py`; `fetched` shows what is present.

## The host's Direct3D

`d3d_runtime` builds DXVK and vkd3d-proton from the forks into `build-d3d/lib`. Changing them is the `av-forks`
skill.

## Before handing a change over

- The gates pass (`test area=gates`) and the unit tests pass (`test area=unit`).
- A change that a guest runs is proven on the GPUs through LSW's `lsw-gpu-verify` skill, once per batch of fixes.
- `gate` runs the whole gate (default, coverage, fuzz and qemu trees). It takes hours: ask the user first.
