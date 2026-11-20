---
name: av-forks
description: Change the host's DXVK or vkd3d-proton - accel-virt's forks acceleration3/dxvk and acceleration3/vkd3d-proton - land a fix there, move accel-virt's submodule to it, take a new upstream release into a fork, manage the dxbc-spirv and dxil-spirv patch series, and send general fixes upstream. Use for any change to third_party/dxvk or third_party/vkd3d-proton.
---

# DXVK and vkd3d-proton forks

`third_party/dxvk` and `third_party/vkd3d-proton` are submodules of the forks. Each fork carries accel-virt's changes
as commits on its `accel-virt` branch, which is the default branch; `av-dev` `forks` shows what is pinned, what is
checked out, the upstream release underneath and how many commits the fork adds. `CONTRIBUTING.md` has the rules for
outside contributors.

## Change a fork

1. In `third_party/<fork>`, on the `accel-virt` branch (tracking `fork/accel-virt`), commit the change like any code.
2. `av-dev` `d3d_runtime` builds it; `test area=gpu` runs the host full-stack tests against it.
3. Push the branch: `git -C third_party/<fork> push fork accel-virt`.
4. In accel-virt, commit the submodule move (`git add third_party/<fork>`), then the superproject change that needs
   it, if any. The gitlink stays mode 160000.

## Take a new upstream release

Merge, never rebase: `git -C third_party/<fork> fetch origin --tags`, then `git merge <tag>` on `accel-virt`, resolve,
build and test, push. The branch refuses force pushes and deletion, so every commit an older accel-virt revision pins
stays reachable. `git diff <tag> accel-virt` shows everything the fork changes.

## The shader compilers

dxbc-spirv (inside DXVK) and dxil-spirv (inside vkd3d-proton) are not forked. Their changes are patches in
`tools/dxbc-spirv-patches/` and `tools/dxil-spirv-patches/`, applied in order by `tools/build_d3d_runtime.sh`. Make a
patch with `git format-patch` inside the subproject, numbered after the last one, and send it upstream too; delete it
when a release has it.

## Upstream first

A fix that any Direct3D user would want goes upstream (doitsujin/dxvk, HansKristian-Work/vkd3d-proton) as well: it
shrinks what the forks carry. Embedder hooks for the executor stay in the forks.
