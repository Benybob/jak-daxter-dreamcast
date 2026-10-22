"""Original activation dependency and explicit argument contract (AI-assisted)."""
import hashlib,json,os
from pathlib import Path
import subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for activation checks')
class FrontendActivationTest(unittest.TestCase):
 def test_unchanged_activation_has_explicit_arguments_and_original_imports(self):
  base=ROOT.parents[1]/'goal_src/jak1/kernel'
  with tempfile.TemporaryDirectory() as folder:
   header=Path(folder)/'activation.h';source=base/'gkernel.gc'
   command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),'--runtime',
            '--source',str(source),'--header',str(header),'--function','(method activate process)']
   for dep in (base/'gcommon.gc',ROOT/'scalar_validation_context.gc',base/'gkernel-h.gc',base/'gstring-h.gc'):
    command+=['--preload',str(dep)]
   result=subprocess.run(command,capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   report=json.loads(header.with_suffix('.manifest.json').read_text())
   self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
   self.assertIsNone(report['initializer']);self.assertEqual(report['static_data_bytes'],0)
   self.assertEqual(len(report['functions']),1)
   imports={r['symbol'] for r in report['functions'][0]['relocations']}
   self.assertTrue({'cell:mem-set32!','cell:change-parent','cell:*kernel-context*','cell:cpu-thread'}<=imports)
   fn=next(f for f in json.loads(header.with_suffix('.ir.json').read_text())['functions'] if f['name']=='(method activate process)')
   self.assertEqual(len(fn['parameters']),4);self.assertEqual(fn['implicit_parameters'],[])
   self.assertNotIn('dc-c-current-process',str(fn))

 def test_word_fill_uses_original_source_without_external_callback(self):
  source=ROOT.parents[1]/'goal_src/jak1/kernel/gcommon.gc'
  with tempfile.TemporaryDirectory() as folder:
   header=Path(folder)/'fill.h'
   result=subprocess.run([sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
    '--runtime','--select-defuns','--source',str(source),'--header',str(header),'--function','mem-set32!'],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   report=json.loads(header.with_suffix('.manifest.json').read_text())
   self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
   self.assertEqual(report['functions'][0]['relocations'],[])
   fn=next(f for f in json.loads(header.with_suffix('.ir.json').read_text())['functions'] if f['name']=='mem-set32!')
   self.assertEqual(len(fn['parameters']),3);self.assertEqual(fn['implicit_parameters'],[])
   self.assertFalse(any(o['kind']=='call' for o in fn['instructions']))
