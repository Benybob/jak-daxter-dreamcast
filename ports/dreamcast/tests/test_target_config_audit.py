"""Target-configuration source audit on synthetic and original kernel text (AI-assisted), ISC."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import audit_target_config as audit

EVIDENCE = PORT / "evidence/target-config-audit.json"
FIXTURE = r'''
(defglobalconstant PC_PORT #t)
(defglobalconstant FAST_PATH (and PC_PORT #t))
; (#when PC_PORT ignored-in-comment)
#| block (#when PC_PORT #| nested |# ignored) |#
(defconstant STACK (#if PC_PORT #x6000 #x1c00))
(defconstant DERIVED (* STACK 2))
(defconstant PLAIN 4)
(defconstant *pad-top* (the pointer #x70004000))
(#cond
  (PC_PORT (let ((m 1)) (define *fake* m)) (defmacro pad-start () '*fake*))
  (else (defmacro pad-start () #x70000000)))
(defmacro pad-end () `(&+ (pad-start) 16))
(defmacro prof (name) `(#when FAST_PATH (log ,name)))
(defun uses-stack () (+ DERIVED PLAIN))
(defun independent () "(#when PC_PORT string)" #\( (+ PLAIN 1))
(defun asm-isa () (declare (asm-func none))
  (#cond ((eq? INSTRUCTION_SET 'arm64) (.ret)) (#t (.ret))))
(defun asm-plain () (declare (asm-func none)) (.ret))
(defun dma () (set! (-> (the pointer #x10008000) 0) 0) (prof "x"))
'''


def run_fixture(text, roots=()):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "fixture.gc"
        path.write_text(text, encoding="utf-8")
        return audit.Audit([path], roots).run()


class TargetConfigAuditTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.report = run_fixture(FIXTURE)
        cls.defs = {(d["name"], d["line"]): d for d in cls.report["dependent_definitions"]}
        cls.uses = {u["form"]: u for u in cls.report["use_sites"]}

    def test_roots_and_derived_config_constants(self):
        self.assertEqual([(r["name"], r["value"]) for r in self.report["roots"]],
                         [("PC_PORT", "#t"), ("FAST_PATH", "(and PC_PORT #t)")])
        self.assertEqual(self.uses["defglobalconstant FAST_PATH"]["names"], {"PC_PORT": [3]})

    def test_comments_strings_and_characters_are_not_code(self):
        lines = {c["line"] for c in self.report["conditionals"]}
        self.assertNotIn(4, lines)
        self.assertNotIn(5, lines)
        self.assertNotIn("defun independent", self.uses)

    def test_dependency_reasons_and_transitive_closure(self):
        self.assertEqual(self.defs[("STACK", 6)]["reason"], "references")
        self.assertEqual(self.defs[("DERIVED", 7)]["via"], ["STACK"])
        self.assertEqual(self.defs[("*pad-top*", 9)]["reason"], "hardware-address")
        self.assertEqual(self.defs[("*fake*", 11)]["reason"], "inside-config-branch")
        self.assertEqual(self.defs[("pad-start", 11)]["via"], ["PC_PORT"])
        self.assertEqual(self.defs[("pad-start", 12)]["reason"], "inside-config-branch")
        self.assertEqual(self.defs[("pad-end", 13)]["via"], ["pad-start"])
        self.assertNotIn("PLAIN", {d["name"] for d in self.report["dependent_definitions"]})
        self.assertEqual(self.uses["defun uses-stack"]["names"], {"DERIVED": [15]})

    def test_template_conditionals_and_macro_users(self):
        prof = [c for c in self.report["conditionals"] if c["form"] == "defmacro prof"]
        self.assertEqual(len(prof), 1)
        self.assertTrue(prof[0]["in_template"])
        self.assertEqual(prof[0]["config_names"], ["FAST_PATH"])
        self.assertEqual(self.uses["defun dma"]["names"], {"prof": [20]})

    def test_hardware_literals_and_inline_assembly(self):
        self.assertEqual([(h["value"], h["region"]) for h in self.report["hardware_addresses"]],
                         [("#x70004000", "scratchpad"), ("#x70000000", "scratchpad"),
                          ("#x10008000", "ee-registers")])
        self.assertEqual([(a["form"], a["instruction_set_conditional"])
                          for a in self.report["asm_functions"]],
                         [("defun asm-isa", True), ("defun asm-plain", False)])
        self.assertEqual(self.report["summary"]["asm_functions_without_instruction_set_branch"], 1)

    def test_region_boundaries(self):
        cases = [(0x0FFFFFFF, None), (0x10000000, "ee-registers"), (0x1000FFFF, "ee-registers"),
                 (0x10010000, None), (0x11000000, "vu-memory"), (0x12001FFF, "gs-privileged"),
                 (0x12002000, None), (0x6FFFFFFF, None), (0x70000000, "scratchpad"),
                 (0x70004000, "scratchpad"), (0x70004001, None)]
        for value, region in cases:
            with self.subTest(value=hex(value)):
                self.assertEqual(audit.hardware_region(value), region)
        self.assertEqual(audit.parse_integer("#x7000_4000"), 0x70004000)
        self.assertIsNone(audit.parse_integer("#xzz"))

    def test_extra_roots(self):
        report = run_fixture("(defconstant X (#when SH4_TARGET 1))\n", ["SH4_TARGET"])
        self.assertEqual(report["dependent_definitions"][0]["via"], ["SH4_TARGET"])
        self.assertEqual(run_fixture("(defconstant X (#when SH4_TARGET 1))\n")["dependent_definitions"], [])

    def test_malformed_source_fails_closed(self):
        for text in ["(defun x ()", ")", '(define s "open)', "#| open", "(quote #\\"]:
            with self.subTest(text=text), self.assertRaises(audit.SourceError):
                audit.read_forms(text)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.gc"
            path.write_text("(defun x ()", encoding="utf-8")
            result = subprocess.run([sys.executable, str(PORT / "audit_target_config.py"),
                                     "--source", str(path)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("unclosed list", result.stderr)

    def test_original_kernel_matches_committed_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            result = subprocess.run([sys.executable, str(PORT / "audit_target_config.py"),
                                     "--output", str(output), "--check", str(EVIDENCE)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
        roots = {r["name"] for r in report["roots"]}
        self.assertLessEqual({"PC_PORT", "BIG_MEMORY", "PC_BIG_MEMORY", "USE_VM",
                              "PC_PROFILER_ENABLE", "KERNEL_DEBUG"}, roots)
        dependent = {d["name"] for d in report["dependent_definitions"]}
        self.assertLessEqual({"scratchpad-start", "*fake-scratchpad-data*", "PROCESS_STACK_SIZE",
                              "PROCESS_HEAP_SIZE", "with-profiler", "get-vm-ptr"}, dependent)
        self.assertTrue(all(a["instruction_set_conditional"] for a in report["asm_functions"]))

    def test_check_reports_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            stale = Path(directory) / "stale.json"
            data = json.loads(EVIDENCE.read_text(encoding="utf-8"))
            data["sources"][0]["sha256_lf"] = "0" * 64
            stale.write_text(json.dumps(data), encoding="utf-8")
            result = subprocess.run([sys.executable, str(PORT / "audit_target_config.py"),
                                     "--check", str(stale)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("differs", result.stderr)


if __name__ == "__main__":
    unittest.main()
