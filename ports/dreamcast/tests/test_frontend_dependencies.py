"""Ordered original source context stays separate from native startup (AI-assisted)."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lower_sh4_ir import compile_function
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for source-context integration checks')
class FrontendDependencyTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.output=Path(self.directory.name)/'fixture.json'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']
        self.source=ROOT/'dependency_fixture.gc'; self.context=ROOT/'dependency_context.gc'

    def run_frontend(self,source,extra):
        return subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(source),str(self.output)]+extra,
                              env=self.env,capture_output=True,text=True)

    def test_missing_context_fails_and_explicit_context_exports_only_main_functions(self):
        self.assertNotEqual(self.run_frontend(self.source,[]).returncode,0)
        result=self.run_frontend(self.source,['--preload',str(self.context)])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        doc=json.loads(self.output.read_text())
        self.assertEqual([Path(p) for p in doc['preloads']],[self.context.resolve()])
        self.assertNotIn('dc-context-helper',[f['name'] for f in doc['functions']])
        functions=[f for f in doc['functions'] if not f['top_level']]
        self.assertEqual(len(functions),4)
        for fn in functions: self.assertTrue(compile_function(fn,runtime=True)[0])

    def test_preload_order_is_semantic_and_recorded(self):
        target=Path(self.directory.name)/'ordered.gc'
        target.write_text('(defun dc-context-order ((arg int)) (dc-context-call arg))\n')
        result=self.run_frontend(target,['--preload',str(self.context),'--preload',str(self.source)])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual([Path(p) for p in json.loads(self.output.read_text())['preloads']],
                         [self.context.resolve(),self.source.resolve()])
        self.assertNotEqual(self.run_frontend(target,['--preload',str(self.source),'--preload',str(self.context)]).returncode,0)

    def test_duplicate_self_missing_and_incomplete_context_arguments_reject(self):
        for extra in (['--preload',str(self.context),'--preload',str(self.context)],
                      ['--preload',str(self.source)],['--preload',str(self.output.parent/'missing.gc')],['--preload']):
            with self.subTest(extra=extra): self.assertNotEqual(self.run_frontend(self.source,extra).returncode,0)

    def test_forward_reference_abi_comes_from_original_declared_parent(self):
        result=self.run_frontend(self.source,['--preload',str(self.context),'dc-context-forward'])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        fn=next(f for f in json.loads(self.output.read_text())['functions'] if f['name']=='dc-context-forward')
        self.assertEqual((fn['return_type'],fn['return_abi_type']),('basic','object'))
        self.assertEqual(fn['parameters'][0]['abi_type'],'object')
        self.assertTrue(any(r['type']=='dc-context-future' and r['abi_type']=='object' for r in fn['registers']))
        self.assertTrue(compile_function(fn,runtime=True)[0])

    def test_compiler_manifest_pins_dependency_path_and_content_hash(self):
        header=self.output.with_suffix('.h')
        result=subprocess.run([sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
             '--source',str(self.source),'--preload',str(self.context),'--function','dc-context-compute',
             '--header',str(header),'--runtime'],env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['preloads'],[{'source':str(self.context.resolve()),
                            'sha256':hashlib.sha256(self.context.read_bytes()).hexdigest()}])

    def test_selected_source_can_preload_its_context_but_full_self_compile_still_rejects(self):
        extra=['--preload',str(self.context),'--preload',str(self.source)]
        result=self.run_frontend(self.source,extra+['dc-context-forward'])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        doc=json.loads(self.output.read_text())
        self.assertTrue(doc['selected_source_preloaded'])
        self.assertEqual([f['name'] for f in doc['functions'] if not f['top_level']],['dc-context-forward'])
        self.assertNotEqual(self.run_frontend(self.source,extra).returncode,0)
        self.assertNotEqual(self.run_frontend(self.source,extra+['--preload',str(self.source),'dc-context-forward']).returncode,0)

    def test_selected_source_preload_manifest_records_explicit_context_and_selection(self):
        header=self.output.with_suffix('.h')
        result=subprocess.run([sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
            '--source',str(self.source),'--preload',str(self.context),'--preload',str(self.source),
            '--select-defuns','--function','dc-context-forward','--header',str(header),'--runtime'],
            env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertTrue(report['selected_source_preloaded'])
        self.assertTrue(report['selected_original_defuns'])
        self.assertEqual([p['sha256'] for p in report['preloads']],
                         [hashlib.sha256(p.read_bytes()).hexdigest() for p in (self.context,self.source)])
