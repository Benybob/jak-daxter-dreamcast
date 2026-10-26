"""Original kernel-dispatcher under the Dreamcast profiler context (AI-assisted), ISC."""
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
KERNEL=PORT.parents[1]/'goal_src/jak1/kernel'

def export(directory,profiler):
    env=os.environ.copy()
    if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
    out=Path(directory)/('noprof.json' if profiler else 'desktop.json')
    deps=[KERNEL/'gcommon.gc',PORT/'scalar_validation_context.gc']+([PORT/'dreamcast_profiler_context.gc'] if profiler else [])
    deps+=[KERNEL/'gkernel-h.gc',KERNEL/'gstring-h.gc']
    cmd=[str(FRONTEND),str(PORT.parents[1]),str(KERNEL/'gkernel.gc'),str(out)]
    for dep in deps:cmd+=['--preload',str(dep)]
    run=subprocess.run(cmd,env=env,capture_output=True,text=True)
    if run.returncode:raise AssertionError(run.stdout+run.stderr)
    doc=json.loads(out.read_text())
    return {f['name']:f for f in doc['functions']},{s['id']:s for s in doc.get('statics',[])}

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for dispatcher checks')
class DispatcherTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.tmp.cleanup)
        cls.desktop=export(cls.tmp.name,False);cls.native=export(cls.tmp.name,True)

    def symbols(self,exported,name):
        module,statics=exported
        _,report=compile_function(module[name],module,True,True,statics)
        return {r['symbol'] for r in report['relocations']}

    def test_profiler_context_removes_desktop_profiler_calls(self):
        for name in ('kernel-dispatcher','anon-function-5'):
            with self.subTest(name=name):
                desktop=json.dumps(self.desktop[0][name]);native=json.dumps(self.native[0][name])
                self.assertNotIn('pc-prof',native)
                self.assertLess(len(self.native[0][name]['instructions']),len(self.desktop[0][name]['instructions']))
                self.assertNotIn('cell:pc-prof',self.symbols(self.native,name))

    def test_dispatcher_resumes_through_the_original_thread_hook(self):
        symbols=self.symbols(self.native,'kernel-dispatcher')
        self.assertLessEqual({'cell:execute-process-tree','cell:*active-pool*','function:anon-function-5'},symbols)
        lam=self.native[0]['anon-function-5']
        # The main-thread resume is an ordinary dynamic call through resume-hook (+16).
        self.assertTrue(any(o['kind']=='load_memory' and o.get('offset')==16 for o in lam['instructions']))

if __name__=='__main__':
    unittest.main()
