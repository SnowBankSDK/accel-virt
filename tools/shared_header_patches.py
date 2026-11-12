#!/usr/bin/env python3
"""Write the patches that carry the shared SPIR-V headers of tools/ into DXVK and vkd3d-proton, or check them."""
import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADERS = (
    {
        "source": ROOT / "tools" / "line-coverage" / "av_line_coverage.h",
        "subject": "the line coverage SPIR-V generator of tools/line-coverage",
        "body": ("The geometry stage that draws a line as a quad carrying its distance from the line, and the\n"
                 "pixel shader change that multiplies the alpha of render target 0 by the coverage of that\n"
                 "distance, as one header shared by DXVK and vkd3d-proton. tools/shared_header_patches.py\n"
                 "writes this patch from tools/line-coverage/av_line_coverage.h.\n"),
        "targets": (
            (ROOT / "tools" / "dxvk-patches" / "0032-the-line-coverage-generator.patch", "src/util/av_line_coverage.h"),
            (ROOT / "tools" / "vkd3d-proton-patches" / "0029-the-line-coverage-generator.patch",
             "libs/vkd3d/av_line_coverage.h"),
        ),
    },
    {
        "source": ROOT / "tools" / "stream-swap" / "av_stream_swap.h",
        "subject": "the geometry stream swap of tools/stream-swap",
        "body": ("The SPIR-V change that trades stream 0 and the rasterized stream of a geometry shader, its\n"
                 "outputs and the vertices and primitives it emits, so that a host without\n"
                 "transformFeedbackRasterizationStreamSelect rasterizes the stream Direct3D names as stream 0,\n"
                 "as one header shared by DXVK and vkd3d-proton. tools/shared_header_patches.py writes this\n"
                 "patch from tools/stream-swap/av_stream_swap.h.\n"),
        "targets": (
            (ROOT / "tools" / "dxvk-patches" / "0035-the-geometry-stream-swap.patch", "src/util/av_stream_swap.h"),
            (ROOT / "tools" / "vkd3d-proton-patches" / "0033-the-geometry-stream-swap.patch",
             "libs/vkd3d/av_stream_swap.h"),
        ),
    },
)


def patch_for(header, text, path):
    lines = text.splitlines()
    out = ["From: accel-virt", f"Subject: [PATCH] {header['subject']}", "", header["body"].rstrip("\n"), "---",
           f"diff --git a/{path} b/{path}", "new file mode 100644", "--- /dev/null", f"+++ b/{path}",
           f"@@ -0,0 +1,{len(lines)} @@"]
    out += ["+" + line for line in lines]
    return "\n".join(out) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a patch differs from what would be written")
    args = parser.parse_args(argv)
    stale = []
    for header in HEADERS:
        text = header["source"].read_text()
        for patch, path in header["targets"]:
            wanted = patch_for(header, text, path)
            if args.check:
                if not patch.exists() or patch.read_text() != wanted:
                    stale.append((patch, header["source"]))
            else:
                patch.write_text(wanted)
    for patch, source in stale:
        print(f"shared headers: {patch.relative_to(ROOT)} is not what {source.relative_to(ROOT)} writes; "
              "run tools/shared_header_patches.py", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
