"""Compile dependency graph behavior on synthetic game.gp forms (AI-assisted)."""
from pathlib import Path
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import gp_deps


class GPDependenciesTest(unittest.TestCase):
    def load_graph(self, text):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'goal_src/jak1/game.gp'
            source.parent.mkdir(parents=True)
            source.write_text(text, encoding='utf-8')
            return gp_deps.load(root)

    def test_goal_src_paths_and_dependency_stems(self):
        graph = self.load_graph('''(begin
          ; ignored source declaration: (goal-src "comment.gc")
          (goal-src "kernel/base.gc")
          (goal-src "engine/child.gc" "base" "$OUT/obj/math.o" "engine/vector.gc")
          (unrelated "ignored.gc"))''')
        self.assertEqual(graph, dict(base=('kernel/base.gc', []),
                                     child=('engine/child.gc', ['base', 'math', 'vector'])))

    def test_sequence_initial_dependencies_and_previous_file_chain(self):
        graph = self.load_graph('''(goal-src-sequence "engine/"
          :deps ("base" "$OUT/obj/types.o") "a.gc" "b.gc" "c.gc")
          (goal-src-sequence "other/" "x.gc" "y.gc")''')
        self.assertEqual(graph['a'], ('engine/a.gc', ['base', 'types']))
        self.assertEqual(graph['b'], ('engine/b.gc', ['a']))
        self.assertEqual(graph['c'], ('engine/c.gc', ['b']))
        self.assertEqual(graph['x'], ('other/x.gc', []))
        self.assertEqual(graph['y'], ('other/y.gc', ['x']))

    def test_transitive_closure_is_dependency_first_without_duplicates_or_root(self):
        graph = self.load_graph('''(begin (goal-src "base.gc")
          (goal-src-sequence "engine/" :deps ("base") "a.gc" "b.gc")
          (goal-src "leaf.gc" "base")
          (goal-src "root.gc" "b" "leaf" "base"))''')
        self.assertEqual(gp_deps.closure(graph, 'root'), ['base', 'a', 'b', 'leaf'])
        self.assertEqual(gp_deps.closure(graph, 'base'), [])

    def test_missing_dependency_is_ignored_but_missing_root_raises(self):
        graph = self.load_graph('''(goal-src "base.gc")
          (goal-src "root.gc" "unknown" "base" "$OUT/obj/also-missing.o")''')
        self.assertEqual(graph['root'][1], ['unknown', 'base', 'also-missing'])
        self.assertEqual(gp_deps.closure(graph, 'root'), ['base'])
        with self.assertRaises(KeyError):
            gp_deps.closure(graph, 'absent-root')

    def test_cycles_refuse(self):
        graph = self.load_graph('(goal-src "a.gc" "b") (goal-src "b.gc" "a")')
        with self.assertRaisesRegex(ValueError, 'dependency cycle'):
            gp_deps.closure(graph, 'a')


if __name__ == '__main__':
    unittest.main()
