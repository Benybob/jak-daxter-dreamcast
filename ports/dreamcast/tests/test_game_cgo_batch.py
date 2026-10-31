"""A failed frontend must not reuse an ignored batch status (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from build_game_cgo import export_batch


class BatchStatusTest(unittest.TestCase):
    def run_batch(self, directory, returncode, status=None):
        path = Path(directory) / 'status.json'
        path.write_text(json.dumps({'exported': ['stale.gc'], 'failed': None}))

        def frontend(*args, **kwargs):
            self.assertFalse(path.exists())
            if status is not None:
                path.write_text(json.dumps(status))
            return subprocess.CompletedProcess(args[0], returncode, '', 'actual frontend error')

        with patch('build_game_cgo.subprocess.run', side_effect=frontend):
            return export_batch('frontend', PORT.parents[1], Path(directory) / 'batch.json',
                                path, PORT / 'dreamcast_memory_budget.gc')

    def test_failed_frontend_cannot_reuse_stale_exports_or_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'frontend exited 1.*batch status') as caught:
                self.run_batch(directory, 1)
            self.assertIn('actual frontend error', str(caught.exception))

    def test_success_exit_without_status_also_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'frontend exited 0.*batch status'):
                self.run_batch(directory, 0)

    def test_fresh_partial_batch_keeps_the_recorded_compile_failure(self):
        status = {'exported': ['new.gc'], 'failed': {'source': 'bad.gc', 'error': 'type error'}}
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.run_batch(directory, 1, status), status)

    def test_nonzero_exit_without_recorded_compile_failure_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'without a recorded compile failure'):
                self.run_batch(directory, 1, {'exported': ['new.gc'], 'failed': None})

    def test_success_uses_fresh_exports(self):
        status = {'exported': ['new.gc'], 'failed': None}
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.run_batch(directory, 0, status), status)


if __name__ == '__main__':
    unittest.main()
