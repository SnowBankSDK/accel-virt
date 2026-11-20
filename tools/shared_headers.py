#!/usr/bin/env python3
"""The SPIR-V generator headers of tools/ are what the DXVK and vkd3d-proton forks carry, or, with --write, copy them there."""
import argparse
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADERS = {
    "line-coverage/av_line_coverage.h": ("dxvk/src/util", "vkd3d-proton/libs/vkd3d"),
    "stream-swap/av_stream_swap.h": ("dxvk/src/util", "vkd3d-proton/libs/vkd3d"),
    "forced-coverage/av_forced_coverage.h": ("vkd3d-proton/libs/vkd3d",),
}


def copies():
    for source, folders in HEADERS.items():
        for folder in folders:
            yield ROOT / "tools" / source, ROOT / "third_party" / folder / pathlib.Path(source).name


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="copy each header into the forks instead of checking")
    args = parser.parse_args(argv)
    stale = []
    for source, copy in copies():
        if args.write:
            shutil.copyfile(source, copy)
        elif not copy.is_file() or copy.read_bytes() != source.read_bytes():
            stale.append((source, copy))
    for source, copy in stale:
        print(f"shared headers: {copy.relative_to(ROOT)} is not {source.relative_to(ROOT)}; run tools/shared_headers.py "
              "--write and commit it in the fork", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
