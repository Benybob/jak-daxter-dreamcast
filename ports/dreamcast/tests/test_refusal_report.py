"""Batch refusal reporting uses synthetic status only (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]


class RefusalReportTest(unittest.TestCase):
    def report(self, errors, *options):
        with tempfile.TemporaryDirectory() as directory:
            status = Path(directory) / 'status.json'
            status.write_text(json.dumps(dict(exported=['success.gc'], export_errors=errors,
                                             failed={'source': 'fatal.gc', 'error': 'fatal'})))
            return subprocess.run([sys.executable, str(PORT / 'refusal_report.py'), str(status),
                                   *options], capture_output=True, text=True, check=True).stdout

    def test_text_groups_exact_errors_by_frequency(self):
        out = self.report([dict(source='z.gc', error='unsupported vf'),
                           dict(source='b.gc', error='missing import'),
                           dict(source='a.gc', error='unsupported vf')])
        self.assertEqual(out.splitlines(), ['2 export refusal(s): unsupported vf', '  a.gc',
                                           '  z.gc', '1 export refusal(s): missing import', '  b.gc'])

    def test_json_and_deterministic_ties(self):
        report = json.loads(self.report([dict(source='z.gc', error='z\ntext'),
                                        dict(source='a.gc', error='a')], '--json'))
        self.assertEqual(report, dict(total=2, groups=[dict(error='a', count=1, files=['a.gc']),
                                                     dict(error='z\ntext', count=1, files=['z.gc'])]))

    def test_no_export_refusals_does_not_count_fatal_failure(self):
        self.assertEqual(self.report([]).strip(), 'No export refusals.')
        self.assertEqual(json.loads(self.report([], '--json')), dict(total=0, groups=[]))

    def test_by_file_text_and_json(self):
        errors = [dict(source='a.gc', error='z'), dict(source='b.gc', error='only'),
                  dict(source='a.gc', error='a\ncontinued')]
        out = self.report(errors, '--by-file')
        self.assertEqual(out.splitlines(), ['a.gc (2 refusal(s))', '  a', '  continued', '  z',
                                           'b.gc (1 refusal(s))', '  only'])
        result = json.loads(self.report(errors, '--json', '--by-file'))
        self.assertEqual(result, dict(total=3, groups=[dict(source='a.gc', count=2, errors=['a\ncontinued', 'z']),
                                                     dict(source='b.gc', count=1, errors=['only'])]))


if __name__ == '__main__':
    unittest.main()
