#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from mcp_server import BuildLimits, Jobs, JobTools, Server, ToolError, state_directory, tool  # noqa: E402

SERVER = {"name": "av-dev", "version": "1"}
QUICK_TIMEOUT = 300
COMPILER = ["-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++"]
PROFILES = {
    "default": ("build", []),
    "coverage": ("build-coverage", ["-DAV_COVERAGE=ON"]),
    "fuzz": ("build-fuzz", ["-DAV_FUZZ=ON"]),
    "qemu": ("build-qemu", ["-DAV_QEMU=ON", "-DCMAKE_BUILD_TYPE=Release"]),
    "stack": ("build-stack", ["-DAV_CHECKED=OFF", "-DAV_SANITIZE=OFF", "-DCMAKE_BUILD_TYPE=Release"]),
}
AREAS = {
    "unit": ("default", ["-LE", "tools|fuzz"]),
    "tools": ("default", ["-L", "tools"]),
    "gates": ("default", ["-R", "^(design_rules|symbol_tests|scope_marks|generated_|self_contained|extension_policy"
                                "|layer_requirements|cts_subset|check_spirv)"]),
    "images": ("default", ["-R", "_image$"]),
    "gpu": ("default", ["-R", "_device$|^test_fullstack_"]),
    "fuzz": ("fuzz", ["-L", "fuzz"]),
    "coverage": ("coverage", []),
    "qemu": ("qemu", []),
}
CHECKS = {
    "design": ["python3", "tools/check_design.py"],
    "symbols": ["python3", "tools/check_symbol_tests.py"],
    "scope_marks": ["python3", "tools/scope_marks.py", "--check"],
    "generated": ["python3", "tools/check_generated.py"],
    "self_contained": ["python3", "tools/check_self_contained.py"],
}
FETCHERS = ["fetch_vulkan_runtime.py", "fetch_opencl_loader.py", "fetch_cts.py"]
FORKS = {"dxvk": "subprojects/dxbc-spirv", "vkd3d-proton": "subprojects/dxil-spirv"}
SERIES = {"dxvk": "tools/dxbc-spirv-patches", "vkd3d-proton": "tools/dxil-spirv-patches"}


class AvDev(JobTools):
    def __init__(self):
        super().__init__(Jobs(state_directory("av-dev") / "jobs"))
        self.limits = BuildLimits()

    def tools(self):
        profile = {"type": "string", "enum": sorted(PROFILES),
                   "description": "default: unit tests and the guest cross-builds; coverage, fuzz and qemu are the "
                                  "gate's trees; stack is the Release executor without checks"}
        return [
            tool("status", "The accel-virt checkout: branch and changes, the DXVK and vkd3d-proton forks, which build "
                 "profiles are configured, the build lock and the running jobs."),
            tool("build", "Configures (when needed) and builds a profile as a background job under the build lock, at the "
                 "limits LSW_BUILD_JOBS, LSW_BUILD_CPU_QUOTA and LSW_BUILD_MEMORY_HIGH set. Targets: all by default; drivers for avgpu.sys and the guest UMDs and ICDs, avgpu, "
                 "avtest, or any CMake target.",
                 {"profile": profile, "target": {"type": "string"}, "reconfigure": {"type": "boolean"}}),
            tool("test", "Runs ctest as a job. area picks a set: unit (every unit and harness test), tools (the "
                 "python tool tests), gates (design, symbols, scope marks, generated sources and the other policy "
                 "checks), images (the PE image rules of every guest binary), gpu (the host full-stack tests on this "
                 "machine's GPU, which pass with 'nothing judged' without one), fuzz (the smoke fuzzers), coverage "
                 "(the 100% branch gate), qemu (the qtest suites). filter narrows it with ctest -R.",
                 {"area": {"type": "string", "enum": sorted(AREAS)}, "filter": {"type": "string"},
                  "fuzz_seconds": {"type": "integer", "description": "fuzz only: seconds per target"}}, ["area"]),
            tool("check", "Runs one source gate right now, without a build, and returns its verdict: design, "
                 "symbols, scope_marks, generated, self_contained.",
                 {"name": {"type": "string", "enum": sorted(CHECKS)}}, ["name"]),
            tool("apply_scope_marks", "Writes the missing AV_SCOPE marks into src/ (tools/scope_marks.py --apply)."),
            tool("regenerate", "Rewrites src/proto/generated from the generators in tools/codegen and the Khronos "
                 "and D3D registries (tools/check_generated.py --write). Never hand-edit what it writes.",
                 {"apis": {"type": "array", "items": {"type": "string"},
                           "description": "vulkan, opencl, opengl, d3d, d3d12, d3d9, glmem; default all"}}),
            tool("d3d_runtime", "Builds DXVK and vkd3d-proton from the forks into build-d3d/lib (a job), applying "
                 "the dxbc-spirv and dxil-spirv series."),
            tool("forks", "The DXVK and vkd3d-proton forks: the commit the superproject pins, what is checked out, "
                 "the upstream release under it, how many commits the fork adds, and the shader compiler series."),
            tool("fetched", "Which third-party downloads are present (each fetcher's --check)."),
            tool("gpus", "This machine's Vulkan physical devices (tools/host/list_gpus.py)."),
            tool("gate", "The whole gate (tools/gate.sh): default, coverage, fuzz and qemu trees built and tested. "
                 "It takes hours: ask the user first.", {}),
            tool("guest_suite", "tools/guest/run_guest_suite.py with the given arguments, as a job: it boots a "
                 "patched QEMU on a copy of a Windows image and uses the GPU. Ask the user first; watch "
                 "gpu_kernel_events during it.",
                 {"arguments": {"type": "array", "items": {"type": "string"}}}, ["arguments"]),
            *self.job_tools(),
        ]

    def run(self, command, timeout=QUICK_TIMEOUT, cwd=ROOT):
        result = subprocess.run(command, cwd=str(cwd), capture_output=True, text=True, timeout=timeout)
        return result.returncode, (result.stdout + result.stderr).strip()

    def git(self, path, *arguments):
        return self.run(["git", "-C", str(path), *arguments], timeout=30)[1]

    def status(self):
        configured = {name: (ROOT / folder / "CMakeCache.txt").is_file() for name, (folder, _) in PROFILES.items()}
        locked = self.run(["flock", "-n", str(self.limits.lock), "true"], timeout=5)[0] != 0
        return {"branch": self.git(ROOT, "rev-parse", "--abbrev-ref", "HEAD"),
                "head": self.git(ROOT, "log", "-1", "--format=%h %s")[:100],
                "changed_files": len(self.git(ROOT, "status", "--porcelain", "--ignore-submodules=dirty").splitlines()),
                "forks": {name: self.fork_summary(name) for name in FORKS},
                "configured_profiles": configured, "build_lock_held": locked,
                "running_jobs": [job for job in self.jobs.list() if job["state"] == "running"]}

    def fork_summary(self, name):
        path = ROOT / "third_party" / name
        pinned = self.git(ROOT, "ls-tree", "HEAD", f"third_party/{name}").split()
        head = self.git(path, "rev-parse", "HEAD")
        return {"pinned": pinned[2][:10] if len(pinned) > 2 else None, "checked_out": head[:10],
                "matches_pin": len(pinned) > 2 and pinned[2] == head,
                "branch": self.git(path, "rev-parse", "--abbrev-ref", "HEAD")}

    def configure(self, profile, reconfigure=False):
        folder, options = PROFILES[profile]
        if (ROOT / folder / "CMakeCache.txt").is_file() and not reconfigure:
            return None
        return ["cmake", "-S", ".", "-B", folder, *COMPILER, *options]

    def build(self, profile="default", target=None, reconfigure=False):
        folder, _ = PROFILES[profile]
        steps = []
        configure = self.configure(profile, reconfigure)
        if configure:
            steps.append(configure)
        steps.append(["cmake", "--build", folder] + (["--target", target] if target else []))
        return self.start(f"build-{profile}", steps)

    def start(self, name, steps, env=None):
        script = " && ".join(" ".join(f"'{part}'" for part in step) for step in steps)
        environment = dict(self.limits.environment())
        environment.update(env or {})
        return self.jobs.start(name, self.limits.wrap(["bash", "-c", script]), ROOT, environment, exclusive="build")

    def test(self, area, filter=None, fuzz_seconds=None):
        profile, selection = AREAS[area]
        folder, _ = PROFILES[profile]
        if not (ROOT / folder / "CMakeCache.txt").is_file():
            raise ToolError(f"{folder} is not configured; build profile {profile} first")
        command = ["ctest", "--test-dir", folder, "--output-on-failure", "-j", self.limits.jobs, *selection]
        if filter:
            command += ["-R", filter]
        env = {"AV_FUZZ_SECONDS": str(fuzz_seconds)} if fuzz_seconds else None
        return self.start(f"test-{area}", [command], env)

    def check(self, name):
        code, output = self.run(CHECKS[name])
        return {"check": name, "passed": code == 0, "output": output[-6000:]}

    def apply_scope_marks(self):
        code, output = self.run(["python3", "tools/scope_marks.py", "--apply"])
        if code != 0:
            raise ToolError(output[-4000:])
        return output[-4000:] or "every definition has its mark"

    def regenerate(self, apis=None):
        command = ["python3", "tools/check_generated.py", "--write"]
        for api in apis or []:
            command += ["--only", api]
        code, output = self.run(command)
        changed = self.git(ROOT, "status", "--porcelain", "src/proto/generated").splitlines()
        return {"passed": code == 0, "output": output[-6000:], "changed_files": len(changed)}

    def d3d_runtime(self):
        return self.start("d3d-runtime", [["tools/build_d3d_runtime.sh"]])

    def forks(self):
        report = {}
        for name, compiler in FORKS.items():
            path = ROOT / "third_party" / name
            summary = self.fork_summary(name)
            base = self.git(path, "describe", "--tags", "--abbrev=0", "HEAD")
            summary.update(upstream_release=base,
                           commits_over_upstream=int(self.git(path, "rev-list", "--count", f"{base}..HEAD") or 0),
                           fork_remote=self.git(path, "remote", "get-url", "fork") or self.git(path, "remote", "get-url",
                                                                                              "origin"),
                           compiler=compiler,
                           compiler_series=sorted(entry.name for entry in (ROOT / SERIES[name]).glob("*.patch")))
            report[name] = summary
        return report

    def fetched(self):
        present = {}
        for fetcher in FETCHERS:
            code, output = self.run(["python3", f"tools/{fetcher}", "--check"], timeout=60)
            present[fetcher] = "present" if code == 0 else (output.splitlines() or ["missing"])[-1]
        present["khronos"] = "present" if (ROOT / "third_party" / "khronos" / "vk.xml").is_file() else "missing"
        present["windows-kit"] = "present" if (ROOT / "third_party" / "windows-kit" / "VERSION").is_file() else "missing"
        present["qemu"] = "present" if any((ROOT / "third_party" / "qemu").glob("*")) else "missing"
        return present

    def gpus(self):
        code, output = self.run(["python3", "tools/host/list_gpus.py"], timeout=60)
        if code != 0:
            raise ToolError(output)
        return output

    def gate(self):
        return self.jobs.start("gate", self.limits.wrap(["tools/gate.sh"]), ROOT, self.limits.environment(),
                               exclusive="build")

    def guest_suite(self, arguments):
        if any(not argument.startswith("-") and index == 0 for index, argument in enumerate(arguments)):
            raise ToolError("arguments are run_guest_suite.py options, starting with --")
        command = ["python3", "-B", "tools/guest/run_guest_suite.py", *arguments]
        return self.jobs.start("guest-suite", command, ROOT, exclusive="vm:av-guest")


def main():
    os.chdir(ROOT)
    Server(AvDev(), SERVER).serve()


if __name__ == "__main__":
    main()
