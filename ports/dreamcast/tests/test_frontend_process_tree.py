"""Original process-tree bodies, dependencies and native field contracts (AI-assisted)."""
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

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for process-tree checks')
class FrontendProcessTreeTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.output=Path(self.directory.name)/'tree.json'
        self.base=ROOT.parents[1]/'goal_src/jak1/kernel'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']

    def frontend(self,source,preloads,extra=()):
        command=[str(FRONTEND),str(ROOT.parents[1]),str(source),str(self.output)]
        for dep in preloads: command+=['--preload',str(dep)]
        result=subprocess.run(command+list(extra),env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return json.loads(self.output.read_text())

    def test_four_original_bodies_have_only_audited_tree_imports_and_pinned_context(self):
        source=self.base/'gkernel.gc'; header=self.output.with_suffix('.h')
        deps=[self.base/'gcommon.gc',ROOT/'scalar_validation_context.gc',self.base/'gkernel-h.gc',
              self.base/'gstring-h.gc',source]
        names=['iterate-process-tree','execute-process-tree','search-process-tree','change-parent']
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(source),'--header',str(header),'--runtime','--select-defuns']
        for dep in deps: command+=['--preload',str(dep)]
        for name in names: command+=['--function',name]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual([p['sha256'] for p in report['preloads']],
                         [hashlib.sha256(p.read_bytes()).hexdigest() for p in deps])
        self.assertTrue(report['selected_source_preloaded'])
        self.assertEqual([f['name'] for f in report['functions']],names)
        self.assertEqual({r['symbol'] for f in report['functions'] for r in f['relocations']},
            {'cell:iterate-process-tree','cell:execute-process-tree','cell:search-process-tree',
             'pointer:#f','pointer:#t','pointer:dead','function:dc-c-call-error'})

    def test_fixture_field_offsets_and_context_size_match_original_declarations(self):
        source=Path(self.directory.name)/'fields.gc'
        names=['name','mask','parent','brother','child','ppointer','self']
        source.write_text('\n'.join(f'(defun dc-tree-field-{name} ((node process-tree)) (-> node {name}))' for name in names)+
                          '\n(defun dc-tree-context-mask ((ctx kernel-context)) (-> ctx prevent-from-run))\n')
        deps=[self.base/'gcommon.gc',self.base/'gkernel-h.gc']
        doc=self.frontend(source,deps)
        functions={f['name']:f for f in doc['functions']}
        for i,name in enumerate(names):
            op=next(o for o in functions['dc-tree-field-'+name]['instructions'] if o['kind']=='load_memory')
            self.assertEqual((op['offset'],op['size']),(i*4,4))
        op=next(o for o in functions['dc-tree-context-mask']['instructions'] if o['kind']=='load_memory')
        self.assertEqual((op['offset'],op['size']),(0,4))
        header=self.frontend(self.base/'gkernel-h.gc',[self.base/'gcommon.gc'])
        top=next(f for f in header['functions'] if f['top_level'])
        self.assertIn((9<<32)|48,[o['value'] for o in top['instructions'] if o['kind']=='constant'])
