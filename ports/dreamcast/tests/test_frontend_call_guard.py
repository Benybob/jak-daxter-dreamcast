"""Original typed call ABI and explicit missing-target exits (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for native call guards')
class FrontendCallGuardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(); cls.addClassCleanup(cls.directory.cleanup)
        cls.header=Path(cls.directory.name)/'calls.h'
        env=os.environ.copy()
        if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
            '--source',str(ROOT/'call_guard_fixture.gc'),'--header',str(cls.header),'--runtime']
        for name in ('identity','float-identity','effect','last','int','float','void','eight','dynamic'):
            command+=['--function','dc-call-guard-'+name]
        result=subprocess.run(command,env=env,capture_output=True,text=True)
        if result.returncode: raise AssertionError(result.stdout+result.stderr)
        cls.report=json.loads(cls.header.with_suffix('.manifest.json').read_text())
        cls.doc=json.loads(cls.header.with_suffix('.ir.json').read_text())

    def test_each_typed_caller_requires_one_strict_fault_bridge_and_false_sentinel(self):
        for fn in self.report['functions']:
            symbols=[r['symbol'] for r in fn['relocations']]
            if fn['name'].split('dc-call-guard-')[1] in ('int','float','void','eight','dynamic'):
                self.assertEqual(symbols.count('function:dc-c-call-error'),1,fn['name'])
                self.assertIn('pointer:#f',symbols)
            else:
                self.assertNotIn('function:dc-c-call-error',symbols)

    def test_call_guard_does_not_change_original_scalar_void_float_or_stack_signatures(self):
        functions={f['name']:f for f in self.doc['functions']}
        for name,ret,args in [('int','int',1),('float','float',1),('void','none',1),('eight','int',8),('dynamic','int',2)]:
            fn=functions['dc-call-guard-'+name]
            self.assertEqual(fn['return_type'],ret)
            self.assertEqual(len(fn['parameters']),args)
            calls=[op for op in fn['instructions'] if op['kind']=='call']
            self.assertEqual(len(calls),1)
            self.assertEqual(calls[0]['abi']['return_type'],ret)
