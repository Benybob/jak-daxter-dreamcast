"""Real C preflight and original arming dependency contract (AI-assisted)."""
import hashlib,json,os
from pathlib import Path
import subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
EXE=ROOT/'build'/('main-thread-check.exe' if sys.platform=='win32' else 'main-thread-check')
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
class MainThreadTest(unittest.TestCase):
 @unittest.skipUnless(EXE.is_file(),'build main-thread checker first')
 def test_real_readonly_state_owner_stack_and_later_allocation(self):
  result=subprocess.run([str(EXE)],capture_output=True,text=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  report=json.loads(result.stdout);self.assertGreaterEqual(report['tests'],90)
  self.assertEqual(report['tests'],report['passed'])
 @unittest.skipUnless(FRONTEND.is_file(),'build original frontend first')
 def test_original_set_to_run_has_eight_scalars_only_data_stores_and_no_implicit_self(self):
  kernel=ROOT.parents[1]/'goal_src/jak1/kernel';source=kernel/'gkernel.gc'
  with tempfile.TemporaryDirectory() as folder:
   header=Path(folder)/'arming.h'
   command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),'--runtime',
            '--source',str(source),'--header',str(header),'--function','set-to-run']
   for dep in (kernel/'gcommon.gc',ROOT/'scalar_validation_context.gc',kernel/'gkernel-h.gc',kernel/'gstring-h.gc'):
    command+=['--preload',str(dep)]
   result=subprocess.run(command,capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   report=json.loads(header.with_suffix('.manifest.json').read_text())
   self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
   self.assertIsNone(report['initializer']);self.assertEqual(report['static_data_bytes'],0)
   self.assertEqual(len(report['functions']),1)
   self.assertEqual({r['symbol'] for r in report['functions'][0]['relocations']},
                    {'pointer:waiting-to-run','cell:set-to-run-bootstrap'})
   fn=next(f for f in json.loads(header.with_suffix('.ir.json').read_text())['functions'] if f['name']=='set-to-run')
   self.assertEqual(len(fn['parameters']),8);self.assertEqual(fn['implicit_parameters'],[])
   self.assertFalse(any(o['kind']=='call' for o in fn['instructions']))
   stores=[(o['offset'],o['size']) for o in fn['instructions'] if o['kind']=='store_memory']
   self.assertEqual(stores,[(32,4),(36,8),(44,8),(52,8),(60,8),(68,8),(76,8),(84,8),(20,4),(24,4)])
