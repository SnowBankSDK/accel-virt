import re
import sys
import unittest

from tool_run import ROOT

sys.path.insert(0, str(ROOT / "tools"))

KIT = ROOT / "third_party" / "windows-kit" / "include"
HEADER = KIT / "um" / "d3dumddi.h"
VERSIONS = KIT / "sdk-shared" / "d3dukmdt.h"
LEVEL = ROOT / "src" / "icd" / "d3d" / "d3d9_level_caps.hpp"
THUNKS = ROOT / "src" / "um" / "d3d" / "generated" / "d3d9_device_thunks.cpp"
GUARD = re.compile(r"#if\s*\(?\s*D3D_UMD_INTERFACE_VERSION\s*(>=|>)\s*(D3D_UMD_INTERFACE_VERSION_\w+)")
WINDOWS_7 = 0x2003
REPLACED_FROM_WDDM_1_3 = {
    "pfnVolBlt": "pfnVolBlt1", "pfnBufBlt": "pfnBufBlt1", "pfnTexBlt": "pfnTexBlt1",
    "pfnCreateResource": "pfnCreateResource2", "pfnPresent": "pfnPresent1", "pfnFlush": "pfnFlush1",
}
EMPTY_SINCE_WINDOWS_7 = {
    "pfnGetCaptureAllocationHandle", "pfnCaptureToSysMem", "pfnLockAsync", "pfnUnlockAsync", "pfnRename",
    "pfnCreateVideoProcessor", "pfnSetVideoProcessBltState", "pfnGetVideoProcessBltStatePrivate",
    "pfnSetVideoProcessStreamState", "pfnGetVideoProcessStreamStatePrivate", "pfnVideoProcessBltHD",
    "pfnDestroyVideoProcessor", "pfnCreateAuthenticatedChannel", "pfnAuthenticatedChannelKeyExchange",
    "pfnQueryAuthenticatedChannel", "pfnConfigureAuthenticatedChannel", "pfnDestroyAuthenticatedChannel",
    "pfnCreateCryptoSession", "pfnCryptoSessionKeyExchange", "pfnDestroyCryptoSession", "pfnEncryptionBlt",
    "pfnGetPitch", "pfnStartSessionKeyRefresh", "pfnFinishSessionKeyRefresh", "pfnGetEncryptionBltKey",
    "pfnDecryptionBlt",
}


def versions():
    out = {}
    for line in VERSIONS.read_text(errors="replace").splitlines():
        matched = re.match(r"#define\s+(D3D_UMD_INTERFACE_VERSION_\w+)\s+(\w+)", line)
        if not matched:
            continue
        value = matched[2]
        out[matched[1]] = int(value, 16) if value.startswith("0x") else out.get(value)
    return out


def guarded(start, end, pattern):
    known = versions()
    lines = HEADER.read_text(errors="replace").splitlines()
    first = next(index for index, line in enumerate(lines) if start in line)
    stack, out = [], []
    for line in lines[first:]:
        text = line.strip()
        if text.startswith("#if"):
            matched = GUARD.match(text)
            floor = 0
            if matched:
                floor = known[matched[2]] + (1 if matched[1] == ">" else 0)
            stack.append(floor)
        elif text.startswith("#else"):
            stack[-1] = 0
        elif text.startswith("#endif"):
            stack.pop()
        elif end in text:
            break
        else:
            matched = pattern.search(text)
            if matched:
                out.append((matched, max(stack, default=0)))
    return out


def reported_level():
    return int(re.search(r"interface_level = (0x[0-9A-Fa-f]+)u", LEVEL.read_text())[1], 16)


def level_constant(name):
    return int(re.search(rf"{name} = (\d+)u", LEVEL.read_text())[1])


@unittest.skipUnless(HEADER.exists(), "the Windows kit is not fetched")
class D3d9DdiLevel(unittest.TestCase):
    def slots(self):
        return [(matched[1], since) for matched, since in
                guarded("typedef struct _D3DDDI_DEVICEFUNCS", "} D3DDDI_DEVICEFUNCS;",
                        re.compile(r"\b(pfn\w+);"))]

    def test_the_header_is_read_with_its_version_guards(self):
        since = dict(self.slots())
        self.assertEqual(since["pfnSetRenderState"], 0)
        self.assertEqual(since["pfnResolveSharedResource"], 0x2003)
        self.assertEqual(since["pfnCreateResource2"], 0x3004)
        self.assertEqual(since["pfnPresent1"], 0x4002)
        self.assertEqual(since["pfnTrimResidencySet"], 0x5002)

    def test_every_slot_the_reported_level_brings_is_filled(self):
        from gen_d3d9_ddi import NULL_SLOTS, RESERVED, SUPERSEDED_SLOTS
        level = reported_level()
        filled = THUNKS.read_text()
        for name, since in self.slots():
            if RESERVED.match(name):
                continue
            with self.subTest(slot=name):
                if since > level or name in SUPERSEDED_SLOTS:
                    continue
                if since <= WINDOWS_7 and name in EMPTY_SINCE_WINDOWS_7:
                    self.assertIn(name, NULL_SLOTS)
                    continue
                self.assertNotIn(name, NULL_SLOTS)
                self.assertIn(f"table.{name} = &avumd_d3d9_{name[3:]};", filled)

    def test_a_slot_the_runtime_replaced_at_the_reported_level_is_empty_and_its_successor_filled(self):
        from gen_d3d9_ddi import SUPERSEDED_SLOTS
        level = reported_level()
        since = dict(self.slots())
        filled = THUNKS.read_text()
        self.assertEqual(SUPERSEDED_SLOTS, REPLACED_FROM_WDDM_1_3)
        self.assertGreaterEqual(level, 0x4002)
        for old, new in SUPERSEDED_SLOTS.items():
            with self.subTest(slot=old):
                self.assertEqual(since[old], 0)
                self.assertLessEqual(since[new], level)
                self.assertNotIn(f"table.{old} =", filled)
                self.assertIn(f"table.{new} = &avumd_d3d9_{new[3:]};", filled)

    def test_no_slot_is_left_empty_without_being_named_here(self):
        from gen_d3d9_ddi import NULL_SLOTS
        level = reported_level()
        newer = {name for name, since in self.slots() if since > level}
        self.assertEqual(set(NULL_SLOTS) - newer, EMPTY_SINCE_WINDOWS_7)

    def test_every_caps_type_the_reported_level_brings_is_known_to_the_caps_handler(self):
        level = reported_level()
        types = guarded("typedef enum _D3DDDICAPS_TYPE", "} D3DDDICAPS_TYPE;",
                        re.compile(r"\b(D3DDDICAPS_\w+)\s*=\s*(\d+)"))
        brought = sorted(int(matched[2]) for matched, since in types if WINDOWS_7 < since <= level)
        self.assertTrue(brought)
        self.assertEqual(brought, list(range(level_constant("first_type"), level_constant("last_type") + 1)))
        names = {int(matched[2]): matched[1] for matched, _ in types}
        self.assertEqual(names[level_constant("architecture_info")], "D3DDDICAPS_GET_ARCHITECTURE_INFO")
        self.assertEqual(names[level_constant("shader_min_precision")], "D3DDDICAPS_GET_SHADER_MIN_PRECISION_SUPPORT")
        self.assertEqual(names[level_constant("multiplane_overlay")], "D3DDDICAPS_GET_MULTIPLANE_OVERLAY_CAPS")
        self.assertEqual(names[level_constant("simple_instancing")], "D3DDDICAPS_GET_SIMPLE_INSTANCING_SUPPORT")
        self.assertEqual(names[level_constant("marker")], "D3DDDICAPS_GET_MARKER_CAPS")


if __name__ == "__main__":
    unittest.main()
