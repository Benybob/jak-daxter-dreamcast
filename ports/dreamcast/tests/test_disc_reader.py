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
        cls.cc = os.environ.get("HOST_CC") or shutil.which("gcc", path=cls.env["PATH"])
        if not cls.cc:
            raise RuntimeError("set HOST_CC to a host compiler with pthread support")
        # Avoid loader dialogs for missing MinGW runtime DLLs (AI-assisted), ISC.
        cls.link_flags = ["-static"] if os.name == "nt" else []
        cls.exe = Path(cls.tmp.name) / ("reader.exe" if os.name == "nt" else "reader")
        subprocess.run([cls.cc, "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-I", str(PORT), str(PORT / "disc_reader.c"),
                        str(PORT / "tests/disc_reader_check.c"), *cls.link_flags, "-o", str(cls.exe)],
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

    def test_temporary_source_mutations_are_rejected(self):
        source = (PORT / "disc_reader.c").read_text()
        copy = "memcpy(q->destination + done, r->bounce, copy);"
        mutations = [
            ("overwrite_completed_on_poll", 0,
             "state = q ? q->state : DC_DISC_INVALID;",
             "state = q ? q->state : DC_DISC_INVALID;\n"
             "  if (q && state == DC_DISC_READING) "
             "memset(r->requests[0].destination, 0, r->requests[0].length);",
             "displayed[i] == pattern(131072+i)"),
            ("route_all_payloads_to_slot_zero", 1, copy,
             "memcpy(r->requests[0].destination + done, r->bounce, copy);",
             "destination[i][j+1] == pattern(i*2048+j)"),
            ("short_successful_prefix", 2, copy,
             "memcpy(q->destination + done, r->bounce, q->length == 40000 ? 1 : copy);",
             "destination[i+1] == pattern(i)"),
            ("publish_failed_as_ready", 2,
             "q->state = ok ? DC_DISC_READY : DC_DISC_FAILED;",
             "q->state = ok ? DC_DISC_READY : DC_DISC_READY;",
             "dc_disc_reader_poll(&f.reader, t) == DC_DISC_FAILED"),
        ]
        for name, group, before, after, assertion in mutations:
            with self.subTest(mutation=name), tempfile.TemporaryDirectory(prefix="disc-mutant-") as tmp:
                self.assertEqual(source.count(before), 1)
                mutated = Path(tmp) / "disc_reader.c"
                mutated.write_text(source.replace(before, after))
                exe = Path(tmp) / ("mutant.exe" if os.name == "nt" else "mutant")
                subprocess.run([self.cc, "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror",
                                "-pthread", "-I", str(PORT), str(mutated),
                                str(PORT / "tests/disc_reader_check.c"), *self.link_flags, "-o", str(exe)],
                               env=self.env, check=True, capture_output=True, text=True)
                result = subprocess.run([str(exe), str(group)], env=self.env, timeout=15,
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0, name + " escaped")
                self.assertIn("Assertion", result.stderr, result.stdout + result.stderr)
                self.assertIn(assertion, result.stderr)
                print(name + ": rejected by assertion")


if __name__ == "__main__":
    unittest.main()
