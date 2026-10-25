"""Shared original-free host/native exact decoder checks (AI-assisted), ISC."""
from pathlib import Path
import subprocess,sys,unittest,re
PORT=Path(__file__).resolve().parents[1]
class SectorInflate(unittest.TestCase):
    def test_exact_framing_output_canaries_and_allocator_failures(self):
        exe=PORT/'build'/('sector-inflate-check.exe' if sys.platform=='win32' else 'sector-inflate-check')
        run=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
        match=re.search(r'inflate (\d+)/(\d+) checks peakdecode=(\d+) failedline=0',run.stdout)
        self.assertIsNotNone(match,run.stdout)
        passed,total,peak=map(int,match.groups());self.assertEqual(passed,total);self.assertGreater(total,100);self.assertLessEqual(peak,65536)
