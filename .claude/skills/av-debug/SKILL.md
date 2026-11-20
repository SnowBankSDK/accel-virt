---
name: av-debug
description: Debug a running accel-virt stack - stalls, hung or crashed guest processes, bugchecks, wrong frames, slow frames - with the av-inspect MCP server (guest and host trace tables, driver log, crashes), a held guest from run_guest_suite.py, traces and profiles. Use when a guest hangs, a sample renders wrong, the driver bugchecks or a frame is slow.
---

# Debug accel-virt live

Look at live state before reading logs: the trace tables name the function every thread is in.

## Where the stack runs

- **Under LSW**: the `lsw` domain. Its QEMU log is the libvirt session log; core dumps are in
  `coredumpctl list | grep qemu`. LSW's `lsw-debug` skill covers the guest side.
- **Under the runner**: `av-dev` `guest_suite` with `--image <img> --work <dir> --executor
  build-qemu/libav_executor.so --keep --no-collect` holds a guest after its run (add `--display gtk`, `--perfetto`,
  `--gdb` or `--record` as needed). Ask the user first: it boots QEMU on the GPU. `tools/guest/guest_shell.py --work
  <dir> ps|exec|push|pull` drives that guest.

## av-inspect

The `av-inspect` MCP server (`tools/mcp/av_inspect.py`) reads both ends:

- **Guest**: `processes`, `threads` (with `stall_seconds`), `app_state`, `contexts`, `handles`, `icd_logs`,
  `crashes` and `crash`, `driver_log` (with `grep`), `guest_powershell`.
- **Kernel**: `kernel_threads`, `kernel_state`, `kernel_objects`, and `kernel_trace_from_memory` over QMP when the
  guest no longer answers.
- **Host**: `host_threads` (device or executor), `host_state`.
- **Names**: `scope_names` and `scope_name` turn trace ids into functions.
- `runs` lists the held runner work directories; `verdict` gives a run's result.

A thread stuck in one scope for seconds is the stall. Read that function before anything else.

## Other evidence

- **A bugcheck**: `tools/guest/read_bugcheck.py` reads it through the gdbstub of a paused VM.
- **A slow frame**: `tools/perfetto_trace.py <qemu pid>` (QEMU started with `AV_PERFETTO=1`), then
  `tools/frame_breakdown.py <trace>`; `tools/profile_sample.sh <sample>` runs both around one sample.
- **Guest pacing and CPU**: `tools/frame_pacing.py` (DWM and flip pacing) and `tools/guest_cpu_profile.py <process>`.
- **Replay**: `build-qemu/avrecord print|replay` for a recorded command stream.
- **A wrong image**: compare against WARP or the host reference first. An artifact that WARP shows too is the
  application's own.

## After a fault

A host GPU fault, ring reset or QEMU crash is a driver bug, never a test to skip: run `gpu_kernel_events`
(av-dev or lsw-dev), keep the evidence and fix the cause. Never retry a GPU-hanging case on the GPU that drives the
user's desktop.
