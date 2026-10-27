"""Compare archive, build report and generated boot order (AI-assisted)."""
import json
from pathlib import Path
import re
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from pack_native_dgo import list_dgo, pack_dgo

DGO = PORT / 'build/kernel-dgo/GAME.CGO'
REPORT = PORT / 'build/game-cgo-report.json'
HEADER = PORT / 'build/game_cgo_order.h'


def check_order(data, report, header):
    names = [obj['name'] for obj in list_dgo(data)['objects']]
    array = re.search(r'\bdc_game_order\s*\[[^\]]*\]\s*=\s*\{(.*?)\}', header, re.S)
    prefix = re.search(r'#define\s+DC_GAME_PREFIX\s+(\d+)u?\b', header)
    if not array or not prefix:
        raise ValueError('missing generated GAME order array or prefix count')
    header_names = [json.loads(token) for token in re.findall(r'"(?:\\.|[^"\\])*"', array[1])]
    if names != report['compiled'] or names != header_names or len(names) != int(prefix[1]):
        raise ValueError('DGO, report and generated header disagree on object order')
    return names


class SyntheticDGOOrderTest(unittest.TestCase):
    def test_two_object_order_and_mismatch_detection(self):
        data = pack_dgo('GAME.CGO', [('first', b'DCSHa'), ('second', b'DCSHb')])
        header = '#define DC_GAME_PREFIX 2u\nstatic const char *const dc_game_order[DC_GAME_PREFIX]={"first","second"};'
        report = dict(compiled=['first', 'second'])
        self.assertEqual(check_order(data, report, header), report['compiled'])
        for wrong_report, wrong_header in [(dict(compiled=['second', 'first']), header),
                                           (report, header.replace('2u', '3u')),
                                           (report, header.replace('"first","second"', '"second","first"')),
                                           (report, '')]:
            with self.subTest(header=wrong_header), self.assertRaises(ValueError):
                check_order(data, wrong_report, wrong_header)


class BuiltDGOOrderTest(unittest.TestCase):
    @unittest.skipUnless(all(p.is_file() for p in (DGO, REPORT, HEADER)), 'build GAME.CGO, report and order header first')
    def test_built_archive_report_and_header_agree(self):
        check_order(DGO.read_bytes(), json.loads(REPORT.read_text()), HEADER.read_text())
