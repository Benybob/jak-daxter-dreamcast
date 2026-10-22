"""Original constructors and exact process layout for native allocation (AI-assisted)."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for process constructor checks')
class FrontendProcessNewTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.output=Path(self.directory.name)/'constructors.json'
        self.base=ROOT.parents[1]/'goal_src/jak1/kernel'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']

    def test_original_methods_only_lower_and_call_sized_object_new_with_three_scalars(self):
        source=self.base/'gkernel.gc'; header=self.output.with_suffix('.h')
        deps=[self.base/'gcommon.gc',ROOT/'scalar_validation_context.gc',self.base/'gkernel-h.gc',self.base/'gstring-h.gc']
        names=['(method new process-tree)','(method new process)']
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(source),'--header',str(header),'--runtime']
        for dep in deps: command+=['--preload',str(dep)]
        for name in names: command+=['--function',name]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual([p['sha256'] for p in report['preloads']],
                         [hashlib.sha256(p.read_bytes()).hexdigest() for p in deps])
        self.assertEqual([f['name'] for f in report['functions']],names)
        self.assertIsNone(report['initializer'])
        self.assertEqual(report['static_data_bytes'],0)
        self.assertEqual({r['symbol'] for f in report['functions'] for r in f['relocations']},
            {'cell:object','cell:process','cell:symbol','pointer:#f','pointer:dead','function:dc-c-call-error'})
        doc=json.loads(header.with_suffix('.ir.json').read_text())
        for fn in (f for f in doc['functions'] if f['name'] in names):
            self.assertFalse(fn['implicit_parameters'])
            calls=[o for o in fn['instructions'] if o['kind']=='call']
            self.assertEqual(len(calls),1)
            self.assertEqual(calls[0]['abi']['parameter_abi_types'],['object','object','int'])

    def test_native_process_fixture_offsets_match_original_declared_fields_and_dynamic_start(self):
        source=Path(self.directory.name)/'fields.gc'
        names=['name','mask','parent','brother','child','ppointer','self','pool','status','pid',
               'main-thread','top-thread','entity','state','trans-hook','post-hook','event-hook',
               'allocated-length','next-state','heap-base','heap-top','heap-cur','stack-frame-top']
        source.write_text('\n'.join(f'(defun dc-new-field-{name} ((node process)) (the-as uint (-> node {name})))' for name in names)+
                          '\n(defun dc-new-stack ((node process)) (the uint (-> node stack)))\n')
        command=[str(FRONTEND),str(ROOT.parents[1]),str(source),str(self.output),
                 '--preload',str(self.base/'gcommon.gc'),'--preload',str(self.base/'gkernel-h.gc')]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        functions={f['name']:f for f in json.loads(self.output.read_text())['functions']}
        for i,name in enumerate(names):
            op=next(o for o in functions['dc-new-field-'+name]['instructions'] if o['kind']=='load_memory')
            self.assertEqual((op['offset'],op['size']),(i*4,4))
        self.assertIn(108,[o['value'] for o in functions['dc-new-stack']['instructions'] if o['kind']=='constant'])
