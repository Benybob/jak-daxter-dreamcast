"""Original catch/protect layouts and constructor access, via real frontend (AI-assisted), ISC."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_stack_objects import FRONTEND,PORT

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend first')
class OriginalCatchSchemaTest(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.tmp.cleanup)
  k=PORT.parents[1]/'goal_src/jak1/kernel';env=os.environ.copy()
  if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
  cls.docs={}
  for source in ['gkernel-h.gc','gkernel.gc']:
   out=Path(cls.tmp.name)/(source+'.json');cmd=[str(FRONTEND),str(PORT.parents[1]),str(k/source),str(out)]
   deps=[k/'gcommon.gc',PORT/'scalar_validation_context.gc']
   if source=='gkernel.gc':deps += [k/'gkernel-h.gc',k/'gstring-h.gc']
   for dep in deps:cmd+=['--preload',str(dep)]
   result=subprocess.run(cmd,env=env,capture_output=True,text=True)
   if result.returncode:raise AssertionError(result.stdout+result.stderr)
   cls.docs[source]=json.loads(out.read_text())
 def test_original_frame_flags_include_complete_basic_header_and_exit(self):
  top=next(f for f in self.docs['gkernel-h.gc']['functions'] if f.get('top_level'))
  values={};types={}
  for op in top['instructions']:
   reads=op.get('reads',[]);writes=op.get('writes',[]);kind=op['kind'];value=None
   if kind=='constant':value=op['value']
   elif kind in ('symbol_value','symbol_pointer'):value=(kind,op['symbol'])
   elif kind=='move':value=values.get(reads[0])
   elif kind=='call' and op.get('function_type')=='(function symbol type int type)':
    args=[values.get(r) for r in reads[1:]]
    if len(args)==3 and args[0] in [('symbol_pointer','catch-frame'),('symbol_pointer','protect-frame'),('symbol_pointer','state')]:
     types[args[0][1]]=(args[1],args[2])
   for dest in writes:values[dest]=value
  self.assertEqual(types['catch-frame'],(('symbol_value','stack-frame'),(9<<32)|176))
  self.assertEqual(types['protect-frame'],(('symbol_value','stack-frame'),(9<<32)|16))
  self.assertEqual(types['state'],(('symbol_value','protect-frame'),(9<<32)|36))
 def test_original_protect_constructor_uses_tagged_header_and_complete_exit_slot(self):
  f=next(f for f in self.docs['gkernel.gc']['functions'] if f['name']=='(method new protect-frame)')
  stores=[o['offset'] for o in f['instructions'] if o['kind']=='store_memory']
  self.assertEqual(stores,[-4,0,8,4,88])
  run=next(f for f in self.docs['gkernel.gc']['functions'] if f['name']=='run-function-in-process')
  self.assertEqual(len(run['parameters']),8)
  self.assertEqual(len([o for o in run['instructions'] if o['kind']=='stack_address']),2)
