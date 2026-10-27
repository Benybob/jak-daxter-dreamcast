"""Synthetic commented local assignments and conservative exclusions (AI-assisted)."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from survey_unassigned_locals import scan_source


class UnassignedLocalsSurveyTest(unittest.TestCase):
    def test_comment_setters_live_writes_strings_and_uncertainty(self):
        source = '''(defun sample ()
          (local-vars (timer int) (live int) (indirect int) (fake int))
          "not a comment ;; (.mfc0 fake Count)"
          ;; (.mfc0 timer Count)
          ; (.lvf live source)
          #| (.mfc0 indirect Count) |#
          (set! live 7)
          (call (& indirect))
          (+ timer live indirect))'''
        result = scan_source(source, 'synthetic.gc')
        self.assertEqual([r['local'] for r in result], ['indirect', 'timer'])
        self.assertEqual(result[0]['status'], 'uncertain')
        self.assertEqual(result[1]['comments'][0]['line'], 4)
        self.assertEqual(result[1]['function'], 'sample')

    def test_live_conditional_asm_setter_excludes_and_nested_scope_is_separate(self):
        source = '''(defun outer () (local-vars (v int))
          ;; (.mfc0 v Count)
          (#cond (PC_PORT (.mov v 1)))
          (lambda () (local-vars (x int))
            ; (.mfc0 x Count)
            x)
          v)'''
        result = scan_source(source)
        self.assertEqual([r['local'] for r in result], ['x'])
