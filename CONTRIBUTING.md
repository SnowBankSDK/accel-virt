# Contributing to accel-virt

Thanks for your interest. accel-virt is a driver stack with strict rules, and a
change lands faster when it follows them from the start.

1. **Read the rules first.** [`CLAUDE.md`](CLAUDE.md) has the C++ rules (freestanding
   C++23, errors as values, everything an object with state, `AV_SCOPE` in every
   function) and [`docs/constitution.md`](docs/constitution.md) the principles behind
   them.
2. **Build and test.** [The README](README.md#building) covers the builds.
   `ctest --test-dir build -LE tools` is the fast loop, and `tools/gate.sh` gives the
   full verdict: design rules, 100% branch coverage of the portable code, the kernel
   cross-build and fuzzing.
3. **Keep a change focused.** One fix or feature per pull request, with its tests.
   Never hand-edit `src/proto/generated/`: change the generator in `tools/codegen/`
   and regenerate.
4. **Say how you tested it.** For driver and executor changes, name the probes,
   samples or suites you ran and on which GPU.

**Changes to DXVK or vkd3d-proton.** The host's Direct3D comes from accel-virt's forks,
[acceleration3/dxvk](https://github.com/acceleration3/dxvk) and
[acceleration3/vkd3d-proton](https://github.com/acceleration3/vkd3d-proton), checked out
under `third_party/`. Each carries accel-virt's changes as commits on its `accel-virt`
branch, the fork's default branch.

- A fix any Direct3D user would want goes to upstream DXVK or vkd3d-proton first. It
  reaches accel-virt when the fork moves to a release that has it, or sooner as a
  cherry-pick.
- A change accel-virt needs, such as a hook for the executor, is a pull request to the
  fork's branch. When it touches accel-virt too, open the accel-virt pull request with the
  submodule pointing at your fork commit, so CI builds both together. The fork pull request
  is merged first, and the submodule then points at the merged commit.
- The `accel-virt` branch is never rewritten: a new upstream release is merged into it,
  so every commit an accel-virt revision points at stays on the branch and that revision
  still builds. `git diff <upstream tag> accel-virt` shows everything the fork changes.
- dxbc-spirv and dxil-spirv, the shader compilers inside DXVK and vkd3d-proton, are not
  forked: their few changes are patches in `tools/dxbc-spirv-patches/` and
  `tools/dxil-spirv-patches/`, applied by `tools/build_d3d_runtime.sh`, until upstream
  has them.

**Reporting bugs.** If you use accel-virt through LSW, report there, with LSW's
log archive. Otherwise use [the bug form](https://github.com/acceleration3/accel-virt/issues/new/choose).

**License.** Contributions are licensed under the [Apache License 2.0](LICENSE), and
files in `src/device/qemu/` under GPL-2.0-or-later, as their headers state.
