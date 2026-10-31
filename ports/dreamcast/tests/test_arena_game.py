"""Original camera layout, conversion and refusal tests (AI-assisted), ISC."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]

class ArenaGameTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="arena-game-")
        cls.env = os.environ.copy()
        if os.name == "nt":
            cls.env["PATH"] += ";C:/msys64/mingw64/bin"
        cls.cc = os.environ.get("HOST_CC") or shutil.which("gcc", path=cls.env["PATH"])
        if not cls.cc:
            raise RuntimeError("set HOST_CC to a host C compiler")
        cls.exes = []
        for mode in ([], ["-ffast-math"]):
            exe = Path(cls.tmp.name) / ("check" + str(len(cls.exes)) + (".exe" if os.name == "nt" else ""))
            subprocess.run([cls.cc, "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror", *mode,
                            "-I", str(PORT), str(PORT / "arena_game.c"), str(PORT / "goal_runtime.c"),
                            str(PORT / "tests/arena_game_check.c"), str(PORT / "render_draw_window.c"),
                            str(PORT / "instance_pass.c"), "-lm", "-o", str(exe)],
                           check=True, env=cls.env, capture_output=True, text=True)
            cls.exes.append(exe)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def group(self, number):
        for exe in self.exes:
            result = subprocess.run([str(exe), str(number)], env=self.env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(exe.name, result.stdout.strip())

    def test_conversion_axes_pitch_units_and_eye(self):
        self.group(0)

    def test_every_float_lane_and_failure_atomicity(self):
        self.group(1)

    def test_native_memory_tags_extents_and_write_transaction(self):
        self.group(2)

    def test_original_goal_layout(self):
        source = (PORT.parents[1] / "goal_src/jak1/engine/gfx/math-camera-h.gc").read_text()
        fields = source.split("(deftype math-camera (basic)", 1)[1].split("(:methods", 1)[0]
        layout, offset = {}, 4
        types = {"meters": (4, 4), "degrees": (4, 4), "float": (4, 4), "int32": (4, 4),
                 "cull-info": (64, 4), "matrix": (64, 16), "vector": (16, 16),
                 "quaternion": (16, 16), "vis-gif-tag": (16, 4), "plane": (16, 16)}
        for name, kind, count, rest in re.findall(r"\(\s*([\w-]+)\s+([\w-]+)\s*(\d*)[^\n]*?([:][^\n]*)?\)", fields):
            if kind not in types or "overlay-at" in rest:
                continue
            size, align = types[kind]
            offset = (offset + align - 1) // align * align
            layout[name] = offset - 4
            offset += size * int(count or 1)
        self.assertEqual(layout["camera-rot"], 364)
        self.assertEqual(layout["inv-camera-rot"], 428)
        self.assertEqual(layout["trans"], 844)
        self.assertEqual(offset, 1060)

if __name__ == "__main__":
    unittest.main()
