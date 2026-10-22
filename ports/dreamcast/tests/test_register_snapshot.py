"""Target object/header compatibility, independently linked by GNU tools (AI-assisted)."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
BIN=Path(os.environ.get('SH4_BIN',str(ROOT.parents[2]/'dc-sdk/sh-elf/bin')))
SUFFIX='.exe' if sys.platform=='win32' else ''
GCC=BIN/('sh-elf-gcc'+SUFFIX)
LD=BIN/('sh-elf-ld'+SUFFIX)
NM=BIN/('sh-elf-nm'+SUFFIX)

@unittest.skipUnless(all(p.is_file() for p in (GCC,LD,NM)),'SH4 SDK compiler/linker required')
class RegisterSnapshotObjectTest(unittest.TestCase):
 def test_header_layout_and_type_contract_in_c99_and_cpp11(self):
  with tempfile.TemporaryDirectory() as folder:
   source=Path(folder)/'layout.c'
   source.write_text('#include "goal_register_snapshot.h"\n'
    'typedef char exact_storage[sizeof(struct dc_goal_native_regs)==60?1:-1];\n'
    'uint32_t (*typed_snapshot)(struct dc_goal_native_regs *)=dc_goal_native_regs_snapshot;\n')
   for language,standard in [('c','c99'),('c++','c++11')]:
    subprocess.run([str(GCC),'-m4-single','-ml','-x',language,'-std='+standard,'-Wall','-Wextra','-Werror',
      '-I',str(ROOT),'-c',str(source),'-o',str(Path(folder)/(language+'.o'))],check=True,capture_output=True)
 def test_assembly_resolves_the_real_c_call_symbol(self):
  with tempfile.TemporaryDirectory() as folder:
   folder=Path(folder);source=folder/'caller.c';caller=folder/'caller.o';snapshot=folder/'snapshot.o';linked=folder/'linked.o'
   source.write_text('#include "goal_register_snapshot.h"\n'
      'uint32_t invoke_snapshot(struct dc_goal_native_regs *p) { return dc_goal_native_regs_snapshot(p); }\n')
   subprocess.run([str(GCC),'-m4-single','-ml','-Wall','-Werror','-c',str(ROOT/'goal_register_snapshot.S'),'-o',str(snapshot)],check=True,capture_output=True)
   subprocess.run([str(GCC),'-m4-single','-ml','-O2','-I',str(ROOT),'-c',str(source),'-o',str(caller)],check=True,capture_output=True)
   subprocess.run([str(LD),'-EL','-r',str(snapshot),str(caller),'-o',str(linked)],check=True,capture_output=True)
   symbols=subprocess.check_output([str(NM),str(linked)],text=True)
   self.assertRegex(symbols,r'(?m)^[0-9a-f]+ T _dc_goal_native_regs_snapshot$')
   self.assertNotRegex(symbols,r'(?m)^\s+U _dc_goal_native_regs_snapshot$')

if __name__=='__main__': unittest.main()
