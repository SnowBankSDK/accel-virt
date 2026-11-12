#!/usr/bin/env python3
"""Write the patches that carry tools/line-coverage into DXVK and vkd3d-proton, or check them."""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tools" / "line-coverage" / "av_line_coverage.h"
SUBJECT = "the line coverage SPIR-V generator of tools/line-coverage"
BODY = ("The geometry stage that draws a line as a quad carrying its distance from the line, and the\n"
        "pixel shader change that multiplies the alpha of render target 0 by the coverage of that\n"
        "distance, as one header shared by DXVK and vkd3d-proton. tools/line_coverage_patches.py\n"
        "writes this patch from tools/line-coverage/av_line_coverage.h.\n")
TARGETS = (
    (ROOT / "tools" / "dxvk-patches" / "0032-the-line-coverage-generator.patch", "src/util/av_line_coverage.h"),
    (ROOT / "tools" / "vkd3d-proton-patches" / "0029-the-line-coverage-generator.patch", "libs/vkd3d/av_line_coverage.h"),
)


def patch_for(text, path):
    lines = text.splitlines()
    out = ["From: accel-virt", f"Subject: [PATCH] {SUBJECT}", "", BODY.rstrip("\n"), "---",
           f"diff --git a/{path} b/{path}", "new file mode 100644", "--- /dev/null", f"+++ b/{path}",
           f"@@ -0,0 +1,{len(lines)} @@"]
    out += ["+" + line for line in lines]
    return "\n".join(out) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a patch differs from what would be written")
    args = parser.parse_args(argv)
    text = SOURCE.read_text()
    stale = []
    for patch, path in TARGETS:
        wanted = patch_for(text, path)
        if args.check:
            if not patch.exists() or patch.read_text() != wanted:
                stale.append(patch)
        else:
            patch.write_text(wanted)
    for patch in stale:
        print(f"line coverage: {patch.relative_to(ROOT)} is not what {SOURCE.relative_to(ROOT)} writes; "
              "run tools/line_coverage_patches.py", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
