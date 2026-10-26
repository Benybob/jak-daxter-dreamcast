"""Source inventory lexical boundaries and DGO ordering (AI-assisted)."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from survey_vu0_usage import count_source, forms, survey


class UsageSurveyTest(unittest.TestCase):
    def test_comments_strings_and_nested_blocks(self):
        report = count_source('''; (.sqc2 vf1 0 a)
          #| (.lqc2 vf1 0 a) #| (rlet ((x :class vf))) |# |#
          (format #t "(.mul.vf x y z) ; #| quoted \\\" .sqc2")
          (rlet ((vf1 :class vf) (vf2 :class vf) (x :class i128))
            (.add.vf vf1 vf1 vf2) (.pextlw x x x)
            (the uint128 x) (fmax 1.0 2.0) (fmin 0.0 1.0)
            (dma-send vif-tag vu1-memory))''')
        self.assertEqual(report['rlet_vf_blocks'], 1)
        self.assertEqual(report['asm_ops'], {'.add.vf': 1, '.pextlw': 1})
        self.assertEqual(report['features'], dict(rlet_i128=1, mmi=1, the_uint128=1,
                                                 fmax=1, fmin=1, dma=1, vif=1, vu1=1))

    def test_quoted_templates_and_conditional_branches_count(self):
        report = count_source("(defmacro x () `(.mul.vf ,x ,y ,z)) (#if PC (.add.vf x y z) (.sub.vf x y z))")
        self.assertEqual(report['asm_ops'], {'.add.vf': 1, '.mul.vf': 1, '.sub.vf': 1})

    def test_invalid_lexical_input_refuses(self):
        for text in ('#|', '(x', ')', '"unterminated'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                forms(text)

    def test_manifest_order_missing_and_ambiguous_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'goal_src/jak1'
            (source / 'dgos').mkdir(parents=True)
            (source / 'dgos/game.gd').write_text('("GAME.CGO" ("b.o" "a.o"))')
            (source / 'a.gc').write_text('(.add.vf x y z)')
            with self.assertRaisesRegex(ValueError, 'found 0'):
                survey(root)
            (source / 'b.gc').write_text('(.mul.vf x y z)')
            report = survey(root)
            self.assertEqual([f['object'] for f in report['files']], ['b.o', 'a.o'])
            self.assertEqual(report['file_count'], 2)
            (source / 'nested').mkdir()
            (source / 'nested/a.gc').write_text('')
            with self.assertRaisesRegex(ValueError, 'found 2'):
                survey(root)

    def test_committed_inventory_matches_sources(self):
        port = Path(__file__).resolve().parents[1]
        report = survey()
        self.assertEqual(report, json.loads((port / 'evidence/vu0-usage.json').read_text()))
        self.assertEqual(len(report['first_40']), 40)
        self.assertEqual(report['files'][3]['object'], 'vector-h.o')
        self.assertNotIn('.vmulax.x', report['files'][3]['asm_ops'])

    def test_data_entries_and_text_hashes_are_cross_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'goal_src/jak1'
            (source / 'dgos').mkdir(parents=True)
            manifest = source / 'dgos/game.gd'
            code = source / 'a.gc'
            manifest.write_bytes(b'("GAME.CGO" ("asset.go" "a.o"))\n')
            code.write_bytes(b'(.mul.vf x y z)\n')
            report = survey(root)
            self.assertEqual(report['source_file_count'], 1)
            self.assertIsNone(report['files'][0]['source'])
            manifest.write_bytes(manifest.read_bytes().replace(b'\n', b'\r\n'))
            code.write_bytes(code.read_bytes().replace(b'\n', b'\r\n'))
            self.assertEqual(report, survey(root))


if __name__ == '__main__':
    unittest.main()
