import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("side_fixture", ROOT / "scripts/surface-mix-side-offset-fixture.py")
side = importlib.util.module_from_spec(spec)
spec.loader.exec_module(side)


class WallWitnessTests(unittest.TestCase):
    def inspect(self, text):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "plate_1.gcode").write_text(text)
            return side.inspect(out)

    def paths(self, feature="Outer wall", tools=True, right=True):
        text = "M83\n;TYPE:" + feature + "\n"
        for tool in range(4):
            if tools:
                text += f"T{tool}\n"
            text += "G0 X104 Y102\nG1 X106 Y102 E0.3\n"
            if right:
                text += "G0 X118 Y102\nG1 X120 Y102 E0.3\n"
        return text

    def test_four_tools_on_both_cells(self):
        self.assertTrue(self.inspect(self.paths()))

    def test_never_assume_initial_tool(self):
        self.assertFalse(self.inspect(self.paths(tools=False)))

    def test_infill_is_not_a_wall(self):
        self.assertFalse(self.inspect(self.paths(feature="Internal solid infill")))

    def test_missing_second_cell_is_not_credited(self):
        self.assertFalse(self.inspect(self.paths(right=False)))

    def test_overlapping_margins_cannot_credit_two_cells(self):
        text = "M83\n;TYPE:Outer wall\n"
        for tool in range(4):
            text += f"T{tool}\nG0 X112.8 Y102\nG1 X113 Y102 E0.3\n"
        self.assertFalse(self.inspect(text))


if __name__ == "__main__":
    unittest.main()
