"""Tests for linear mips2c lint (AI-assisted), ISC license; see LICENSE."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1]/'lint_mips2c.py'
spec = importlib.util.spec_from_file_location('lint_mips2c', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class LintTests(unittest.TestCase):
    def test_fake_tree(self):
        source = '''namespace Mips2C::jak1 {
namespace sample {
void helper(ExecutionContext* c, u16* vis) {
  vis[5] = 1;
  vis[5] = 2;
  int used = vis[5];
  vis[5] = 3;
  vis[5] = vis[5] + 1;
  vis[vi07] = 4;
}
u64 execute(void* ctxt) {
  auto* c = (ExecutionContext*)ctxt;
  c->sq(gp, 12448, at);
  get_fake_spad_addr(at, cache.x, 0, c);
  c->sq(a0, 12448, at);
  c->sd(ra, 8, sp); c->sq(s2, 16, sp);
  c->daddiu(t4, a0, 1);
  c->addu(t5, t4, t4);
  helper(c, vis);
  c->gprs[t0].du64[0] = vis[6];
  c->gprs[t0].du64[0] = vis[5];
  c->gprs[t0].du64[0] = vis[vi07];
  // c->sq(gp, 0, v1); vis[5] = 2;
  /* c->sq(gp, 0, v1);
     vis[5] = 2; */
}
namespace nested {
void helper(ExecutionContext* c, u16* vis) { vis[6] = 0; }
}
}
}'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root/'game/mips2c/jak1_functions/example.cpp'
            path.parent.mkdir(parents=True); path.write_text(source)
            rows = module.lint(root)
            simple = {(r['function'], r['kind'], r['register_or_index'], r['line']) for r in rows}
            self.assertEqual(simple, {
                ('Mips2C::jak1::sample::execute', 'register_read_before_write', 'gp', 13),
                ('Mips2C::jak1::sample::execute', 'register_read_before_write', 'at', 13),
                ('Mips2C::jak1::sample::execute', 'vis_read_without_helper_write', 6, 20),
                ('Mips2C::jak1::sample::helper', 'vis_overwritten_without_read', 5, 5)})
            run = subprocess.run([sys.executable, str(SCRIPT), '--root', str(root),
                                  str(path.relative_to(root))], capture_output=True, text=True, check=True)
            self.assertEqual(json.loads(run.stdout), rows)
            self.assertTrue(all(r['file'] == path.relative_to(root).as_posix() for r in rows))

    def test_sources_and_transitive_helpers(self):
        source = '''namespace test {
void leaf(ExecutionContext* c, u16* vis) { vis[6] = 1; }
void helper(ExecutionContext* c, u16* vis) { leaf(c, vis); }
u64 execute(void* ctxt) {
c->daddiu(t4, t4, 1);
c->gprs[v0].du64[0] = c->gpr_src(t4).du64[0];
c->sw(v0, 4, a0);
helper(c, vis);
int x = vis[6];
}
}'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); path = root/'test.cpp'; path.write_text(source)
            rows = module.lint(root, ['test.cpp'])
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]['register_or_index'], 't4')
            self.assertEqual(rows[0]['line'], 5)

if __name__ == '__main__':
    unittest.main()
