"""Original structured compiler images and kernel-context range provenance (AI-assisted)."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(ROOT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for structured-static checks')
class FrontendStructuredTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.header=Path(self.directory.name)/'structured.h'
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']
    def run_wrapper(self,source,deps,extra):
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(source),'--runtime','--header',str(self.header)]
        for dep in deps: command+=['--preload',str(dep)]
        result=subprocess.run(command+extra,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return json.loads(self.header.with_suffix('.manifest.json').read_text()),json.loads(self.header.with_suffix('.ir.json').read_text())

    def test_structured_images_keep_original_bytes_and_all_four_relocation_kinds(self):
        extra=['--initializer','dc-structured-init']
        for name in ('get','get-plain','value','flags','child','serial','call','count','get-record',
                     'callback-root','callback-child','callback-plain'): extra+=['--function','dc-structured-'+name]
        context=ROOT/'structured_static_context.gc'
        report,doc=self.run_wrapper(ROOT/'structured_static_fixture.gc',[context],extra)
        self.assertEqual(report['static_data_bytes'],104)
        self.assertEqual(len(report['functions']),13)
        images=doc['statics']; self.assertEqual(len(images),4)
        self.assertEqual(sorted(len(i['data_bytes']) for i in images),[8,32,32,32])
        self.assertEqual({r['symbol'].split(':')[0] for i in images for r in i['relocations']},
                         {'type','pointer','function','data'})
        self.assertEqual(sorted(i['address_offset'] for i in images),[0,4,4,4])
        self.assertEqual(report['preloads'][0]['sha256'],hashlib.sha256(context.read_bytes()).hexdigest())

    def test_original_kernel_context_is_exactly_one_original_define_with_pinned_dependencies(self):
        base=ROOT.parents[1]/'goal_src/jak1/kernel'; source=base/'gkernel.gc'
        deps=[base/'gcommon.gc',ROOT/'scalar_validation_context.gc',base/'gkernel-h.gc',base/'gstring-h.gc']
        report,doc=self.run_wrapper(source,deps,['--from-global','*kernel-context*','--prefix-through-global','*kernel-context*',
                                               '--initializer','dc-original-context-init'])
        self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual([p['sha256'] for p in report['preloads']],[hashlib.sha256(p.read_bytes()).hexdigest() for p in deps])
        self.assertEqual(report['source_selection'],{'kind':'range_through_global','from_global':'*kernel-context*',
                                                   'global':'*kernel-context*','top_level_forms':1})
        self.assertEqual(len(report['functions']),1); self.assertEqual(report['static_data_bytes'],48)
        image=doc['statics'][0]
        self.assertEqual(struct.unpack_from('<I',bytes(image['data_bytes']),4)[0],65)
        self.assertEqual(struct.unpack_from('<I',bytes(image['data_bytes']),16)[0],2)
        self.assertEqual(image['relocations'],[{'offset':0,'symbol':'type:kernel-context'},
            {'offset':24,'symbol':'pointer:#f'},{'offset':28,'symbol':'pointer:#f'},{'offset':44,'symbol':'pointer:#t'}])
