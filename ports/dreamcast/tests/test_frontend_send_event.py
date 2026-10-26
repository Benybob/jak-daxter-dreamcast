"""Original send-event-function lexical pp bracket export and lowering (AI-assisted), ISC."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
PORT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(PORT))
from lower_sh4_ir import compile_function
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(PORT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for lexical pp bracket checks')
class SendEventTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.tmp.cleanup)
        kernel=PORT.parents[1]/'goal_src/jak1/kernel'
        cls.source=kernel/'gstate.gc';cls.header=Path(cls.tmp.name)/'send.h'
        env=os.environ.copy()
        if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
        cmd=[sys.executable,str(PORT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),'--runtime',
             '--source',str(cls.source),'--function','send-event-function','--header',str(cls.header)]
        for dep in [kernel/'gcommon.gc',PORT/'scalar_validation_context.gc',kernel/'gkernel-h.gc',
                    kernel/'gstring-h.gc',kernel/'gkernel.gc']:
            cmd+=['--preload',str(dep)]
        run=subprocess.run(cmd,env=env,capture_output=True,text=True)
        if run.returncode:raise AssertionError(run.stdout+run.stderr)
        cls.doc=json.loads(cls.header.with_suffix('.ir.json').read_text())
        cls.fn=next(f for f in cls.doc['functions'] if f['name']=='send-event-function')
        cls.report=json.loads(cls.header.with_suffix('.manifest.json').read_text())

    def test_unchanged_original_exports_one_lexical_role(self):
        self.assertEqual([b['role'] for b in self.fn['fixed_registers']],['process_lexical'])
        self.assertEqual(self.report['source_sha256'],hashlib.sha256(self.source.read_bytes()).hexdigest())
        writes=[o for o in self.fn['instructions'] if self.fn['fixed_registers'][0]['register'] in o['writes']]
        self.assertEqual([o['kind'] for o in writes],['fixed_register_reset','move','move'])

    def test_lowering_links_guarded_scope_bridges(self):
        symbols={r['symbol'] for f in self.report['functions'] for r in f['relocations']}
        self.assertLessEqual({'function:dc-c-current-process','function:dc-c-enter-process-scope',
                              'function:dc-c-leave-process-scope','function:dc-c-call-error','cell:valid?'},symbols)
        for optimize in (True,False):
            data,_=compile_function(self.fn,runtime=True,optimize=optimize)
            self.assertGreater(len(data),0)

if __name__=='__main__':
    unittest.main()
