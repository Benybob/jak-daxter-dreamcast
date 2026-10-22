"""Bounded original startup uses contiguous reader forms (AI-assisted)."""
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

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for startup-prefix checks')
class FrontendPrefixTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory(); self.addCleanup(self.directory.cleanup)
        self.source=Path(self.directory.name)/'prefix.gc'; self.output=self.source.with_suffix('.json')
        self.env=os.environ.copy()
        if os.name=='nt': self.env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+self.env['PATH']

    def run_frontend(self,extra):
        return subprocess.run([str(FRONTEND),str(ROOT.parents[1]),str(self.source),str(self.output)]+extra,
                              env=self.env,capture_output=True,text=True)

    def test_prefix_retains_contiguous_order_and_never_compiles_later_forms(self):
        self.source.write_text('(define dc-prefix-first 0)\n(set! dc-prefix-first 42)\n'
            '(define dc-prefix-stop (+ dc-prefix-first 1))\n'
            '(dc-prefix-unavailable-operation)\n(define dc-prefix-later 99)\n')
        self.assertNotEqual(self.run_frontend([]).returncode,0)
        result=self.run_frontend(['--prefix-through-global','dc-prefix-stop'])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        doc=json.loads(self.output.read_text())
        self.assertEqual(doc['source_selection'],{'kind':'prefix_through_global','global':'dc-prefix-stop','top_level_forms':3})
        self.assertEqual(len(doc['functions']),1)
        self.assertEqual([op['symbol'] for op in doc['functions'][0]['instructions'] if op['kind']=='set_symbol'],
                         ['dc-prefix-first','dc-prefix-first','dc-prefix-stop'])

    def test_missing_ambiguous_nested_and_incompatible_selections_reject(self):
        self.source.write_text('(define dc-prefix-stop 1)\n(define dc-prefix-stop 2)\n')
        self.assertNotEqual(self.run_frontend(['--prefix-through-global','dc-prefix-stop']).returncode,0)
        self.source.write_text('(define dc-prefix-stop 1)\n(defun dc-prefix-function () 2)\n')
        for extra in (['--prefix-through-global','missing'],['--prefix-through-global'],
            ['--prefix-through-global','dc-prefix-function'],
            ['--prefix-through-global','dc-prefix-stop','dc-prefix-function'],
            ['--prefix-through-global','dc-prefix-stop','--prefix-through-global','dc-prefix-stop'],
            ['--preload',str(self.source),'--prefix-through-global','dc-prefix-stop']):
            with self.subTest(extra=extra): self.assertNotEqual(self.run_frontend(extra).returncode,0)
        self.source.write_text('(#when #t (define dc-prefix-stop 1))\n')
        self.assertNotEqual(self.run_frontend(['--prefix-through-global','dc-prefix-stop']).returncode,0)

    def test_original_kernel_prefix_manifest_pins_boundary_source_and_dependencies(self):
        source=ROOT.parents[1]/'goal_src/jak1/kernel/gkernel.gc'; header=self.source.with_suffix('.h')
        deps=[ROOT.parents[1]/'goal_src/jak1/kernel/gcommon.gc',ROOT/'scalar_validation_context.gc',
              ROOT.parents[1]/'goal_src/jak1/kernel/gkernel-h.gc',ROOT.parents[1]/'goal_src/jak1/kernel/gstring-h.gc']
        command=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
                 '--source',str(source),'--header',str(header),'--prefix-through-global','*last-loado-debug-usage*',
                 '--initializer','dc-prefix-original-init','--runtime']
        for dep in deps: command+=['--preload',str(dep)]
        result=subprocess.run(command,env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        report=json.loads(header.with_suffix('.manifest.json').read_text())
        self.assertEqual(report['source_sha256'],hashlib.sha256(source.read_bytes()).hexdigest())
        self.assertEqual(report['source_selection']['kind'],'prefix_through_global')
        self.assertEqual(report['source_selection']['global'],'*last-loado-debug-usage*')
        self.assertEqual([p['sha256'] for p in report['preloads']],
                         [hashlib.sha256(p.read_bytes()).hexdigest() for p in deps])
        doc=json.loads(header.with_suffix('.ir.json').read_text())
        self.assertEqual(len(doc['functions']),1)
        self.assertEqual([op['symbol'] for op in doc['functions'][0]['instructions'] if op['kind']=='set_symbol'],
            ['*use-old-listener-print*','*kernel-version*','*irx-version*','*kernel-boot-mode*',
             '*kernel-boot-level*','*deci-count*','*last-loado-length*','*last-loado-global-usage*','*last-loado-debug-usage*'])
        self.assertEqual(doc['statics'],[])

    def test_native_prefix_wrapper_requires_explicit_initializer_and_excludes_defun_selection(self):
        self.source.write_text('(define dc-prefix-stop 1)\n(defun dc-prefix-function () 2)\n')
        base=[sys.executable,str(ROOT/'compile_goal_subset.py'),'--frontend',str(FRONTEND),
              '--source',str(self.source),'--header',str(self.source.with_suffix('.h')),
              '--prefix-through-global','dc-prefix-stop','--function','dc-prefix-function']
        for extra in ([],['--initializer','dc-prefix-init','--select-defuns']):
            self.assertNotEqual(subprocess.run(base+extra,env=self.env,capture_output=True,text=True).returncode,0)

    def test_inclusive_global_range_retains_middle_forms_and_records_both_boundaries(self):
        self.source.write_text('(dc-unavailable-before)\n(define dc-range-start 10)\n'
            '(set! dc-range-start (+ dc-range-start 1))\n(define dc-range-end dc-range-start)\n'
            '(dc-unavailable-after)\n')
        result=self.run_frontend(['--from-global','dc-range-start','--prefix-through-global','dc-range-end'])
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        doc=json.loads(self.output.read_text())
        self.assertEqual(doc['source_selection'],{'kind':'range_through_global','from_global':'dc-range-start',
                          'global':'dc-range-end','top_level_forms':3})
        self.assertEqual([op['symbol'] for op in doc['functions'][0]['instructions'] if op['kind']=='set_symbol'],
                         ['dc-range-start','dc-range-start','dc-range-end'])

    def test_global_range_requires_unique_ordered_boundaries_and_no_other_selection(self):
        self.source.write_text('(define dc-range-end 1)\n(define dc-range-start 2)\n')
        self.assertNotEqual(self.run_frontend(['--from-global','dc-range-start','--prefix-through-global','dc-range-end']).returncode,0)
        self.source.write_text('(define dc-range-start 1)\n(define dc-range-end 2)\n(define dc-range-start 3)\n')
        self.assertNotEqual(self.run_frontend(['--from-global','dc-range-start','--prefix-through-global','dc-range-end']).returncode,0)
        self.source.write_text('(define dc-range-start 1)\n(define dc-range-end 2)\n')
        for extra in (['--from-global','dc-range-start'],['--from-global'],
            ['--from-global','missing','--prefix-through-global','dc-range-end'],
            ['--from-global','dc-range-start','--prefix-through-global','missing'],
            ['--from-global','dc-range-start','--from-global','dc-range-start','--prefix-through-global','dc-range-end'],
            ['--from-global','dc-range-start','--prefix-through-global','dc-range-end','some-defun'],
            ['--preload',str(self.source),'--from-global','dc-range-start','--prefix-through-global','dc-range-end']):
            with self.subTest(extra=extra): self.assertNotEqual(self.run_frontend(extra).returncode,0)
