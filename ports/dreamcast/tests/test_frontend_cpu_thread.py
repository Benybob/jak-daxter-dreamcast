"""Original CPU-thread constructor and precise persisted data layout (AI-assisted)."""
import hashlib,json,os
from pathlib import Path
import subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for CPU-thread checks')
class FrontendCpuThreadTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base=ROOT.parents[1]/'goal_src/jak1/kernel'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']

    def test_unchanged_constructor_is_six_scalar_arguments_without_callbacks_or_pp(self):
        source=self.base/'gkernel.gc'; header=Path(self.directory.name)/'cpu.h'
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(source),'--header',str(header),'--runtime']
        for dep in (self.base/'gcommon.gc',ROOT/'scalar_validation_context.gc',self.base/'gkernel-h.gc',self.base/'gstring-h.gc'):
            command+=['--preload',str(dep)]
        for name in ('(method new cpu-thread)','(method asize-of cpu-thread)'): command+=['--function',name]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertIsNone(report['initializer']); self.assertEqual(report['static_data_bytes'],0)
        self.assertEqual({r['symbol'] for f in report['functions'] for r in f['relocations']},{'pointer:#f'})
        fn=next(f for f in json.loads(header.with_suffix('.ir.json').read_text())['functions'] if f['name']=='(method new cpu-thread)')
        self.assertEqual(len(fn['parameters']),6); self.assertEqual(fn['implicit_parameters'],[])
        self.assertFalse(any(o['kind']=='call' for o in fn['instructions']))

    def test_original_thread_fields_saved_registers_and_stack_offsets_match_bounded_native_schema(self):
        names=['name','process','previous','suspend-hook','resume-hook','pc','sp','stack-top','stack-size']
        source=Path(self.directory.name)/'fields.gc'; output=source.with_suffix('.json')
        source.write_text('\n'.join(f'(defun dc-cpu-{name} ((x cpu-thread)) (the-as uint (-> x {name})))' for name in names)+
            '\n(defun dc-cpu-rreg ((x cpu-thread)) (-> x rreg 0))\n(defun dc-cpu-freg ((x cpu-thread)) (-> x freg 0))\n'+
            '(defun dc-cpu-stack ((x cpu-thread)) (the uint (-> x stack)))\n')
        command=[str(FRONTEND),str(ROOT.parents[1]),str(source),str(output)]
        for dep in (self.base/'gcommon.gc',self.base/'gkernel-h.gc'): command+=['--preload',str(dep)]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        functions={f['name']:f for f in json.loads(output.read_text())['functions']}
        for i,name in enumerate(names):
            op=next(o for o in functions['dc-cpu-'+name]['instructions'] if o['kind']=='load_memory')
            self.assertEqual((op['offset'],op['size']),(i*4,4))
        for name,offset,size in [('rreg',36,8),('freg',92,4)]:
            op=next(o for o in functions['dc-cpu-'+name]['instructions'] if o['kind']=='load_memory')
            self.assertEqual((op['offset'],op['size']),(offset,size))
        self.assertIn(124,[o['value'] for o in functions['dc-cpu-stack']['instructions'] if o['kind']=='constant'])
        command=[str(FRONTEND),str(ROOT.parents[1]),str(self.base/'gkernel-h.gc'),str(output),
                 '--preload',str(self.base/'gcommon.gc')]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        top=next(f for f in json.loads(output.read_text())['functions'] if f['top_level'])
        self.assertIn((12<<32)|128,[o['value'] for o in top['instructions'] if o['kind']=='constant'])
