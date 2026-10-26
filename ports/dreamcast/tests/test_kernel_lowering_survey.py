"""Committed original kernel lowering coverage stays current (AI-assisted), ISC."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
PORT=Path(__file__).resolve().parents[1]
FRONTEND=Path(os.environ.get('GOAL_IR_FRONTEND',str(PORT.parents[2]/'og-host/bin/goalc-dreamcast-ir.exe')))
EVIDENCE=PORT/'evidence/kernel-lowering-survey.json'

@unittest.skipUnless(FRONTEND.is_file(),'build original frontend for kernel lowering coverage')
class KernelLoweringSurveyTest(unittest.TestCase):
    def test_coverage_matches_committed_evidence(self):
        run=subprocess.run([sys.executable,str(PORT/'survey_kernel_lowering.py'),'--frontend',str(FRONTEND),
                            '--check',str(EVIDENCE)],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_evidence_is_self_consistent(self):
        report=json.loads(EVIDENCE.read_text(encoding='utf-8'))
        for name,data in report['files'].items():
            with self.subTest(file=name):
                self.assertEqual(data['total']-data['lowered'],len(data['refused']))
        self.assertEqual(report['summary']['lowered'],sum(f['lowered'] for f in report['files'].values()))
        self.assertEqual(sum(report['summary']['reasons'].values()),
                         report['summary']['total']-report['summary']['lowered'])
        self.assertEqual(report['files']['gstate.gc']['refused'],{})

if __name__=='__main__':
    unittest.main()
