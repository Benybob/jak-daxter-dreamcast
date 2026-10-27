"""Synthetic C registration parsing, including comments and multiline calls (AI-assisted)."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from kernel_services import desktop_services, native_services


class KernelServicesTest(unittest.TestCase):
    def test_desktop_calls_methods_duplicates_and_comment_exclusion(self):
        text = '''// make_function_symbol_from_c("ignore", (void*)bad);
        void make_function_symbol_from_c(const char* name, void* f) { }
        make_function_symbol_from_c("file-read", (void*)ns::read);
        make_stack_arg_function_symbol_from_c(
          "format", (void*)format_impl);
        auto method = make_function_from_c((void*)copy_basic, true);
        /* make_function_from_c((void*)ignored); */
        make_function_symbol_from_c("file-read", (void*)ns::read);
        '''
        rows = desktop_services(text)
        self.assertEqual([(r['goal'], r['function']) for r in rows],
                         [('file-read', 'ns::read'), ('format', 'format_impl'),
                          (None, 'copy_basic'), ('file-read', 'ns::read')])
        self.assertEqual(rows[0]['line'], 3)
        self.assertEqual(desktop_services('make_func_symbol_func("timer", (void*)read_timer);')[0]['goal'], 'timer')

    def test_native_tables_singletons_and_method_installs(self):
        text = '''dc_goal_code_export exports[2]={{"a", code, 12, 0, 0},
                                                 {"b", code, 12, 0, 0}};
        dc_goal_code_export one={"single", code, 12, 0, 0};
        const char *unrelated="not-exported";
        dc_goal_set_method(rt, rt->pair_type, 0, entry);
        dc_goal_method_set(rt, rt->basic_type, 6, BRIDGE("b"), 1);'''
        result = native_services(text)
        self.assertEqual([r['goal'] for r in result['exports']], ['a', 'b', 'single'])
        self.assertEqual(len(result['methods']), 2)
        self.assertEqual(result['methods'][1]['arguments'][3], 'BRIDGE("b")')
