import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "mcp"))
import av_dev  # noqa: E402


class AvDevTest(unittest.TestCase):
    def test_the_gate_s_trees_are_the_profiles_of_the_same_name(self):
        gate = (ROOT / "tools" / "gate.sh").read_text()
        entries = re.findall(r'^\s*"(\w+) (\S+) (\S+)\s*([^"]*)"', gate, re.M)
        self.assertTrue(entries)
        for name, folder, _selection, options in entries:
            with self.subTest(profile=name):
                profile_folder, profile_options = av_dev.PROFILES[name]
                self.assertEqual(profile_folder, folder)
                self.assertTrue(set(options.split()) <= set(profile_options))

    def test_every_tool_it_lists_is_implemented(self):
        server = av_dev.AvDev()
        for entry in server.tools():
            with self.subTest(tool=entry["name"]):
                self.assertTrue(callable(getattr(server, entry["name"], None)))

    def test_every_test_area_names_a_profile(self):
        for area, (profile, _selection) in av_dev.AREAS.items():
            with self.subTest(area=area):
                self.assertIn(profile, av_dev.PROFILES)


if __name__ == "__main__":
    unittest.main()
