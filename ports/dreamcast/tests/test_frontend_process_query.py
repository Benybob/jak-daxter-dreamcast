"""Original query callback closure, comparisons and startup slices (AI-assisted)."""
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
from lower_sh4_ir import compile_module
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for process query checks')
class FrontendProcessQueryTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.base=ROOT.parents[1]/'goal_src/jak1/kernel'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']

    def build(self,source,stem,names=(),extra=(),string_context=False):
        header=Path(self.directory.name)/(stem+'.h')
        deps=[self.base/'gcommon.gc',self.base/'gstring-h.gc'] if string_context else [
            self.base/'gcommon.gc',ROOT/'scalar_validation_context.gc',self.base/'gkernel-h.gc',self.base/'gstring-h.gc']
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--runtime','--source',str(source),'--header',str(header)]
        for dep in deps: command+=['--preload',str(dep)]
        for name in names: command+=['--function',name]
        result=subprocess.run(command+list(extra),env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual([p['sha256'] for p in report['preloads']],[hashlib.sha256(p.read_bytes()).hexdigest() for p in deps])
        return report,json.loads(header.with_suffix('.ir.json').read_text())

    def test_query_wrappers_keep_original_anonymous_callbacks_and_require_closed_selection(self):
        names=['process-by-name','process-not-name','process-count','anon-function-0','anon-function-1','anon-function-2']
        report,doc=self.build(self.base/'gkernel.gc','queries',names)
        self.assertFalse(report['selected_original_defuns'])
        self.assertEqual([f['name'] for f in report['functions']],names)
        for name in names[:3]:
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,'unresolved function object'):
                compile_module(doc,[name],runtime=True)
        self.assertEqual({r['symbol'] for f in report['functions'] for r in f['relocations']},
            {'function:anon-function-0','function:anon-function-1','function:anon-function-2',
             'cell:*global-search-name*','cell:*global-search-count*','cell:*null-kernel-context*',
             'cell:search-process-tree','cell:iterate-process-tree','cell:name=','pointer:#f','pointer:#t','function:dc-c-call-error'})

    def test_comparison_bodies_use_original_byte_access_and_symbol_string_offset(self):
        report,doc=self.build(self.base/'gstring.gc','compare',['string=','name='],['--select-defuns'],True)
        self.assertTrue(report['selected_original_defuns'])
        functions={f['name']:f for f in doc['functions']}
        loads=[o for o in functions['string=']['instructions'] if o['kind']=='load_memory']
        self.assertTrue(loads)
        self.assertTrue(all(o['size']==1 and not o['signed'] for o in loads))
        self.assertIn(0x20000,[o['value'] for o in functions['name=']['instructions'] if o['kind']=='constant'])
        self.assertEqual({r['symbol'] for f in report['functions'] for r in f['relocations']},
                         {'cell:string','cell:symbol','cell:string=','pointer:#f','pointer:#t','function:dc-c-call-error'})

    def test_original_null_context_and_search_globals_initialize_only_requested_forms(self):
        report,doc=self.build(self.base/'gkernel.gc','null',extra=['--from-global','*null-kernel-context*',
            '--prefix-through-global','*null-kernel-context*','--initializer','dc-query-null-init'])
        self.assertEqual(report['static_data_bytes'],48)
        self.assertEqual(len(report['functions']),1)
        report,doc=self.build(self.base/'gkernel.gc','globals',extra=['--from-global','*global-search-name*',
            '--prefix-through-global','*global-search-count*','--initializer','dc-query-search-init'])
        self.assertEqual(report['static_data_bytes'],0)
        writes=[o['symbol'] for f in doc['functions'] for o in f['instructions'] if o['kind']=='set_symbol']
        self.assertEqual(writes,['*global-search-name*','*global-search-count*'])
