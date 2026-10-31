"""Host worker and deterministic late-read checks (AI-assisted), ISC."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]


class DiscReaderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="disc-reader-")
        cls.env = os.environ.copy()
        if os.name == "nt":
            cls.env["PATH"] += ";C:/msys64/mingw64/bin"
        cc = os.environ.get("HOST_CC") or shutil.which("gcc", path=cls.env["PATH"])
        if not cc:
            raise RuntimeError("set HOST_CC to a host compiler with pthread support")
        cls.exe = Path(cls.tmp.name) / ("reader.exe" if os.name == "nt" else "reader")
        subprocess.run([cc, "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-I", str(PORT), str(PORT / "disc_reader.c"),
                        str(PORT / "tests/disc_reader_check.c"), "-o", str(cls.exe)],
                       env=cls.env, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def check_group(self, group):
        result = subprocess.run([str(self.exe), str(group)], env=self.env, timeout=15,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())

    def test_late_multichunk_read_keeps_previous_complete_output(self):
        self.check_group(0)

    def test_queue_capacity_fifo_and_teardown(self):
        self.check_group(1)

    def test_io_failures_never_publish_ready(self):
        self.check_group(2)

    def test_invalid_requests_and_generation_exhaustion(self):
        self.check_group(3)


if __name__ == "__main__":
    unittest.main()
