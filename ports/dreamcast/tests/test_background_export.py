"""Host exporter ownership/CLI contract without game data (AI-assisted), ISC."""
from pathlib import Path
import subprocess,tempfile,unittest
EXE=Path(__file__).resolve().parents[1]/'build/export-background.exe'
@unittest.skipUnless(EXE.exists(),'build the optional C++ background exporter first')
class BackgroundExport(unittest.TestCase):
    def test_bad_lods_and_existing_outputs_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'bad.raw';source.write_bytes(b'not a game asset')
            target=root/'out.glb';partial=root/'out.glb.partial'
            def run(a,b):return subprocess.run([str(EXE),str(source),str(target),a,b],capture_output=True,text=True)
            for a,b in (('-1','0'),('2x','3'),('2','4'),('','3'),('99999999999999999999','3')):
                self.assertNotEqual(run(a,b).returncode,0);self.assertFalse(target.exists());self.assertFalse(partial.exists())
            target.write_bytes(b'keep');self.assertNotEqual(run('2','3').returncode,0);self.assertEqual(target.read_bytes(),b'keep')
            target.unlink();partial.write_bytes(b'keep partial');self.assertNotEqual(run('2','3').returncode,0);self.assertEqual(partial.read_bytes(),b'keep partial');self.assertFalse(target.exists())
