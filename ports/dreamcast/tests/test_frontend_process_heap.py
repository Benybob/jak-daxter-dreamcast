"""Typed process selector calls use the measured native bridge ABIs (AI-assisted)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for process allocation checks')
class FrontendProcessHeapTest(unittest.TestCase):
    def test_process_basic_and_original_tree_dispatch_keep_explicit_typed_calls(self):
        with tempfile.TemporaryDirectory() as directory:
            header=Path(directory)/'process.h'
            command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                '--source',str(ROOT/'process_heap_fixture.gc'),'--runtime','--header',str(header)]
            for dep in ('goal_src/jak1/kernel/gcommon.gc','ports/dreamcast/scalar_validation_context.gc',
                        'goal_src/jak1/kernel/gkernel-h.gc','goal_src/jak1/kernel/gstring-h.gc'):
                command+=['--preload',str(ROOT.parents[1]/dep)]
            for name in ('create','read','tree'): command+=['--function','dc-process-heap-'+name]
            env=os.environ.copy()
            if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
            result=subprocess.run(command,env=env,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            doc=json.loads(header.with_suffix('.ir.json').read_text())
            functions={f['name']:f for f in doc['functions']}
            call=next(op for op in functions['dc-process-heap-create']['instructions'] if op['kind']=='call')
            self.assertEqual(call['abi']['parameter_abi_types'],['object','object'])
            call=next(op for op in functions['dc-process-heap-tree']['instructions'] if op['kind']=='call')
            self.assertEqual(call['abi']['parameter_abi_types'],['object','object','object'])
            load=next(op for op in functions['dc-process-heap-read']['instructions'] if op['kind']=='load_memory')
            self.assertEqual((load['offset'],load['size'],load['signed']),(4,8,True))
            report=json.loads(header.with_suffix('.manifest.json').read_text())
            self.assertIsNone(report['initializer'])
            self.assertEqual(len(report['functions']),3)
