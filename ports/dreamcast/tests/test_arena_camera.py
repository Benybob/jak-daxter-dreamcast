"""Probe diagnostics with host doubles, not original execution (AI-assisted), ISC."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]


class ArenaCameraTest(unittest.TestCase):
    def test_observer_refuses_without_original_writes(self):
        env = os.environ.copy()
        if os.name == "nt":
            env["PATH"] += ";C:/msys64/mingw64/bin"
        cc = os.environ.get("HOST_CC") or shutil.which("gcc", path=env["PATH"])
        self.assertTrue(cc, "set HOST_CC to a host compiler")
        # The target stores native addresses in u32. Keep these HOST TEST DOUBLES
        # below 4 GiB; this flag does not change any Dreamcast build.
        low_image = ["-Wl,--image-base,0x10000000", "-Wl,--disable-dynamicbase"] if os.name == "nt" else ["-no-pie"]
        with tempfile.TemporaryDirectory(prefix="arena-camera-") as tmp:
            exe = Path(tmp) / ("probe.exe" if os.name == "nt" else "probe")
            subprocess.run([cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-DDC_ARENA_CAMERA", *low_image, "-I", str(PORT),
                            str(PORT / "tests/arena_camera_check.c"), str(PORT / "goal_runtime.c"),
                            str(PORT / "goal_module.c"), "-o", str(exe)],
                           env=env, check=True, capture_output=True, text=True)
            result = subprocess.run([str(exe)], env=env, timeout=15, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
