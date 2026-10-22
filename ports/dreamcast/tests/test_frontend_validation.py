"""Explicit Dreamcast validation context and original source selection (AI-assisted)."""
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

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for validation-context checks')
class FrontendValidationTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.header=Path(self.directory.name)/'validation.h'
        self.original=ROOT.parents[1]/'goal_src/jak1/kernel/gcommon.gc'
        self.context=ROOT/'scalar_validation_context.gc'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']

    def compile(self,source,names,preloads,selected=False):
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(source),'--header',str(self.header),'--runtime']
        for path in preloads: command+=['--preload',str(path)]
        for name in names: command+=['--function',name]
        if selected: command+=['--select-defuns']
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return json.loads(self.header.with_suffix('.ir.json').read_text()),json.loads(self.header.with_suffix('.manifest.json').read_text())

    def test_original_validator_body_stays_in_original_source_with_pinned_ordered_context(self):
        before=hashlib.sha256(self.original.read_bytes()).hexdigest()
        doc,report=self.compile(self.original,['valid?'],[self.original,self.context],True)
        self.assertEqual(hashlib.sha256(self.original.read_bytes()).hexdigest(),before)
        self.assertEqual(report['source_sha256'],before)
        self.assertEqual([p['sha256'] for p in report['preloads']],
                         [before,hashlib.sha256(self.context.read_bytes()).hexdigest()])
        self.assertTrue(report['selected_source_preloaded'])
        functions=[f for f in doc['functions'] if not f['top_level']]
        self.assertEqual([f['name'] for f in functions],['valid?'])
        blob,lowered=compile_function(functions[0],runtime=True,statics={s["id"]:s for s in doc["statics"]})
        self.assertGreater(len(blob),0)
        self.assertTrue(lowered['far_branches'] and lowered['literal_islands'])
        # Printing is an explicit import, not a silent no-op; native tests suppress it.
        self.assertTrue(any(r['symbol']=='cell:format' for r in lowered['relocations']))

    def test_context_order_replaces_desktop_bound_only_when_explicitly_selected(self):
        fixture=ROOT/'scalar_validation_fixture.gc'
        def ram_constant(preloads):
            doc,_=self.compile(fixture,['dc-validation-ram-end'],preloads)
            fn=next(f for f in doc['functions'] if f['name']=='dc-validation-ram-end')
            return [op['value'] for op in fn['instructions'] if op['kind']=='constant']
        self.assertEqual(ram_constant([self.original]),[0x08000000])
        self.assertEqual(ram_constant([self.original,self.context]),[0x0d000000])
        self.assertEqual(ram_constant([self.context,self.original]),[0x08000000])
