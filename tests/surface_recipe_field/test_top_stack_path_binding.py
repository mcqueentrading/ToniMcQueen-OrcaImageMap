import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("top_fixture", ROOT / "scripts/surface-mix-top-stack-fixture.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class PathBindingTests(unittest.TestCase):
    def check_gcode(self, *, wrong_tool=False, missing=False, no_tools=False, shift=0.0, wrong_z=False, absolute=False):
        with tempfile.TemporaryDirectory(prefix="surfacemix-path-test-") as temp:
            root = Path(temp)
            cells = [{"surface_to_deep": [fixture.COMPONENTS[(d + cell) % 4] for d in range(19)]} for cell in range(2)]
            fixture.write_json(root / "fixture-manifest.json", {"cells": cells})
            lines = ["M82" if absolute else "M83", "G92 E0"]
            total = 0.0
            for depth in reversed(range(19)):
                z = 3.2 - depth * 0.08 + shift
                lines += [f";Z:{z}", ";HEIGHT:0.08", f"G1 Z{z + (0.04 if wrong_z else 0)}"]
                for cell, x in enumerate((100, 114)):
                    if missing and depth == 7 and cell == 1:
                        continue
                    tool = fixture.COMPONENTS.index(cells[cell]["surface_to_deep"][depth])
                    if wrong_tool and depth == 7 and cell == 1:
                        tool = (tool + 1) % 4
                    if not no_tools:
                        lines.append(f"T{tool}")
                    total += 0.08
                    lines += [f"G0 X{x+3} Y104", f"G1 X{x+9} Y104 E{total if absolute else '.08'}"]
            (root / "plate_1.gcode").write_text("\n".join(lines) + "\n")
            result = fixture.inspect_gcode(root)
            return result, json.loads((root / "path-binding-report.json").read_text())

    def test_complete_relative_extrusions(self):
        self.assertTrue(self.check_gcode()[0])

    def test_complete_absolute_extrusions(self):
        self.assertTrue(self.check_gcode(absolute=True)[0])

    def test_wrong_component_is_not_a_coverage_pass(self):
        passed, report = self.check_gcode(wrong_tool=True)
        self.assertFalse(passed)
        self.assertEqual(len(report["conflicts"]), 1)

    def test_missing_extrusion_remains_missing(self):
        passed, report = self.check_gcode(missing=True)
        self.assertFalse(passed)
        self.assertEqual(report["missing"], [{"cell": 1, "depth": 7}])

    def test_unobserved_tools_are_not_assumed_to_be_zero(self):
        passed, report = self.check_gcode(no_tools=True)
        self.assertFalse(passed)
        self.assertEqual(len(report["conflicts"]), 38)
        self.assertIsNone(report["conflicts"][0]["actual_tool"])

    def test_shifted_intervals_cannot_round_into_a_pass(self):
        passed, report = self.check_gcode(shift=0.02)
        self.assertFalse(passed)
        self.assertEqual(len(report["missing"]), 38)

    def test_commanded_z_must_match_the_layer(self):
        passed, report = self.check_gcode(wrong_z=True)
        self.assertFalse(passed)
        self.assertTrue(report["interval_conflicts"])


if __name__ == "__main__":
    unittest.main()
