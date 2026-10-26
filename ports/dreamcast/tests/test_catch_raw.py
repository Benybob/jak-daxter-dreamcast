"""Real SH4 C/C++ linkage and catch image ABI (AI-assisted), ISC."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_register_snapshot import ROOT,GCC,LD,NM

@unittest.skipUnless(all(p.is_file() for p in (GCC,LD,NM)),'SH4 SDK compiler/linker required')
class CatchRawObjectTest(unittest.TestCase):
 def test_c_and_cpp_callers_link_to_exact_assembly_symbols(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);asm=d/'catch.o'
   subprocess.run([str(GCC),'-m4-single','-ml','-c',str(ROOT/'native_catch.S'),'-o',str(asm)],check=True,capture_output=True)
   for language,standard in [('c','c99'),('c++','c++11')]:
    source=d/'caller.c';obj=d/(language+'.o');linked=d/(language+'-linked.o')
    source.write_text('#include "goal_catch_raw.h"\n'
     'uint64_t caller(dc_goal_catch_raw *p,uint64_t (*cb)(void *),void *a) {return dc_goal_catch_invoke_raw(p,cb,a);}\n'
     'void escape(dc_goal_catch_raw *p) { dc_goal_catch_escape_raw(p); }\n'
     'const void *site=dc_goal_catch_invoke_return;\n')
    subprocess.run([str(GCC),'-m4-single','-ml','-x',language,'-std='+standard,'-Wall','-Wextra','-Werror','-O2','-I',str(ROOT),'-c',str(source),'-o',str(obj)],check=True,capture_output=True)
    subprocess.run([str(LD),'-EL','-r',str(asm),str(obj),'-o',str(linked)],check=True,capture_output=True)
    symbols=subprocess.check_output([str(NM),str(linked)],text=True)
    for name in ['dc_goal_catch_invoke_raw','dc_goal_catch_escape_raw','dc_goal_catch_invoke_return']:
     self.assertRegex(symbols,r'(?m)^[0-9a-f]+ T _'+name+'$')
     self.assertNotRegex(symbols,r'(?m)^\s+U _'+name+'$')
 def test_installed_compiler_proves_layout_and_distinct_result_area(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=Path(tmp);source=d/'layout.c'
   source.write_text('#include "goal_catch_raw.h"\n'
    'typedef char exact_regs[sizeof(((dc_goal_catch_raw *)0)->regs)==60?1:-1];\n'
    'typedef char exact_padding[offsetof(dc_goal_catch_raw,reserved)==60?1:-1];\n'
    'typedef char exact_result[offsetof(dc_goal_catch_raw,result)==64?1:-1];\n'
    'typedef char exact_total[sizeof(dc_goal_catch_raw)==72?1:-1];\n')
   for language,standard in [('c','c99'),('c++','c++11')]:
    subprocess.run([str(GCC),'-m4-single','-ml','-x',language,'-std='+standard,'-Wall','-Wextra','-Werror','-I',str(ROOT),'-c',str(source),'-o',str(d/(language+'.o'))],check=True,capture_output=True)
