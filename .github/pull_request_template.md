## What this changes

<!-- What the change does, and why. Link the issue it fixes: "Fixes #123". -->

## How it was tested

<!-- The tests you added, and for driver or executor changes the probes, samples or suites you ran, on which GPU. -->

## Checklist

- [ ] `ctest --test-dir build -LE tools` passes, and `tools/gate.sh` for changes to `src/core`, `src/miniport`, `src/proto`, `src/device` or `src/icd`.
- [ ] New branches are covered by tests; nothing new is marked `AV_UNTESTABLE` without a reason.
- [ ] Generated code under `src/proto/generated/` was regenerated with `tools/codegen/`, not edited by hand.
- [ ] A protocol ID or wire change is called out above, since both sides must move together.
- [ ] Every new function opens with `AV_SCOPE` (`tools/scope_marks.py --apply`).
- [ ] The code follows [`CLAUDE.md`](https://github.com/acceleration3/accel-virt/blob/master/CLAUDE.md) and [`docs/constitution.md`](https://github.com/acceleration3/accel-virt/blob/master/docs/constitution.md).
