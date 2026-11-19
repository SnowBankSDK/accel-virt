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

**Reporting bugs.** If you use accel-virt through LSW, report there, with LSW's
log archive. Otherwise use [the bug form](https://github.com/acceleration3/accel-virt/issues/new/choose).

**License.** Contributions are licensed under the [Apache License 2.0](LICENSE), and
files in `src/device/qemu/` under GPL-2.0-or-later, as their headers state.
