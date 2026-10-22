"""Check emitted machine bytes against GNU assembler, not an emulator model."""
import os
from pathlib import Path
import struct
import re
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from sh4_backend import Emitter,probes
from lower_sh4_ir import FLOAT_ARGUMENT_REGISTERS,compile_function
from shift_sh4 import emit_shift64
BIN=Path(os.environ.get('SH4_BIN',str(ROOT.parents[2]/'dc-sdk/sh-elf/bin')))
EXE='.exe' if sys.platform=='win32' else ''
ASSEMBLER=BIN/('sh-elf-as'+EXE)
OBJCOPY=BIN/('sh-elf-objcopy'+EXE)
GCC=BIN/('sh-elf-gcc'+EXE)

@unittest.skipUnless(GCC.is_file(),'SH-4 GCC required for independent ABI oracle')
class GccAbiOracleTest(unittest.TestCase):
    def test_all_eight_64_bit_integer_argument_positions(self):
        params=','.join('unsigned long long a'+str(i) for i in range(8))
        source='\n'.join(f'unsigned long long native_arg{i}({params}) {{ return a{i}; }}' for i in range(8))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); c=root/'oracle.c'; asm=root/'oracle.s'; c.write_text(source)
            subprocess.run([str(GCC),'-O2','-m4-single','-ml','-S',str(c),'-o',str(asm)],check=True,capture_output=True)
            assembly=asm.read_text()
            for i in range(8):
                body=re.search(r'_native_arg'+str(i)+r':(.*?)\.size',assembly,re.DOTALL)[1]
                if i<2:
                    self.assertRegex(body,r'mov\s+r'+str(4+i*2)+r',r0')
                    self.assertRegex(body,r'mov\s+r'+str(5+i*2)+r',r1')
                else:
                    offset=(i-2)*8
                    pattern=r'mov.l\s+@r15,r0' if not offset else r'mov.l\s+@\('+str(offset)+r',r15\),r0'
                    self.assertRegex(body,pattern)
                    self.assertRegex(body,r'mov.l\s+@\('+str(offset+4)+r',r15\),r1')

    def test_all_eight_single_precision_argument_positions(self):
        parameters=','.join('float a'+str(i) for i in range(8))
        source='\n'.join(f'float native_float_arg{i}({parameters}) {{ return a{i}; }}' for i in range(8))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); c=root/'oracle.c'; asm=root/'oracle.s'
            c.write_text(source)
            subprocess.run([str(GCC),'-O2','-m4-single','-ml','-S',str(c),'-o',str(asm)],check=True,capture_output=True)
            assembly=asm.read_text()
            for i,expected in enumerate(FLOAT_ARGUMENT_REGISTERS):
                body=re.search(r'_native_float_arg'+str(i)+r':(.*?)\.size',assembly,re.DOTALL)
                self.assertIsNotNone(body)
                actual=re.search(r'fmov\s+fr(\d+),fr0',body[1])
                self.assertIsNotNone(actual)
                self.assertEqual(int(actual[1]),expected)

class EmitterValidationTest(unittest.TestCase):
    def test_reject_bad_register_immediate_and_literal(self):
        for reg in (-1,16):
            with self.assertRaises(ValueError): Emitter().mov(reg,0)
        for value in (-129,128):
            with self.assertRaises(ValueError): Emitter().immediate(0,value)
        for value in (-(1<<31)-1,1<<32):
            with self.assertRaises(ValueError): Emitter().constant(0,value)

    def test_branch_and_pool_range_failures(self):
        e=Emitter(); e.branch('bf','missing')
        with self.assertRaisesRegex(ValueError,'undefined'): e.finish()
        e=Emitter(); e.label('same')
        with self.assertRaisesRegex(ValueError,'duplicate'): e.label('same')
        e=Emitter(); e.branch('bf','far')
        for _ in range(130): e.nop()
        e.label('far')
        with self.assertRaisesRegex(ValueError,'branch range'): e.finish()
        e=Emitter(); e.constant(0,0x12345678)
        for _ in range(600): e.nop()
        with self.assertRaisesRegex(ValueError,'literal pool range'): e.finish()

    def test_symbol_relocation_and_repeatable_finish(self):
        e=Emitter(); e.call_symbol('bridge'); e.ret()
        data,relocations=e.finish()
        self.assertEqual((data,relocations),e.finish())
        self.assertEqual(relocations,[{'offset':16,'symbol':'bridge'}])
        self.assertEqual(data[16:20],b'\0'*4)

@unittest.skipUnless(ASSEMBLER.is_file() and OBJCOPY.is_file(),'SH-4 binutils required for independent assembler oracle')
class AssemblerOracleTest(unittest.TestCase):
    def test_compact_native_tail_bridge(self):
        e=Emitter(); e.constant(0,0x12345678); e.word(0x402b); e.nop()
        self.assertEqual(e.finish()[0],self.assemble('mov.l .Lentry,r0\njmp @r0\nnop\n.align 2\n.Lentry:\n.long 0x12345678'))
        self.assertEqual(len(e.finish()[0]),12)

    def test_float_predicate_conversion_and_square_root_encodings(self):
        e=Emitter(); e.rr(0xf004,0,1); e.rr(0xf005,0,1)
        for word in (0x405a,0xf02d,0xf03d,0x005a,0xf06d): e.word(word)
        self.assertEqual(e.finish()[0],self.assemble('fcmp/eq fr1,fr0\nfcmp/gt fr1,fr0\n'
                         'lds r0,fpul\nfloat fpul,fr0\nftrc fr0,fpul\nsts fpul,r0\nfsqrt fr0'))

    def test_relaxed_forward_backward_conditions_preserve_literals(self):
        e=Emitter(); e.label('start'); e.branch('bt','end'); e.constant(0,0x12345678)
        for _ in range(130): e.nop()
        e.label('end'); e.branch('bf','start'); e.ret()
        oracle=self.assemble('.Lstart:\nbf .Lskip\nbra .Lend\nnop\n.Lskip:\nmov.l .Lvalue,r0\n'
                             '.rept 130\nnop\n.endr\n.Lend:\nbt .Lbackskip\nbra .Lstart\nnop\n'
                             '.Lbackskip:\nrts\nnop\n.align 2\n.Lvalue:\n.long 0x12345678')
        self.assertEqual(e.finish(relax=True)[0],oracle)
        self.assertEqual(e.instruction_size(relax=True),len(e.code)+8)
        self.assertEqual(e.finish(relax=True),e.finish(relax=True))

    def test_literal_island_matches_gnu_branch_over_aligned_data(self):
        e=Emitter(); e.constant(0,0x12345678)
        for _ in range(600): e.nop()
        e.ret()
        oracle=self.assemble('mov.l .Lvalue,r0\n.rept 496\nnop\n.endr\nbra .Lresume\nnop\n'
                             '.align 2\n.Lvalue:\n.long 0x12345678\n.Lresume:\n.rept 104\nnop\n.endr\nrts\nnop')
        code,relocs,stats=e.finish_with_stats(literal_islands=True)
        self.assertEqual(code,oracle); self.assertEqual(relocs,[])
        self.assertEqual(stats,dict(instruction_bytes=len(e.code)+4,expanded_conditional_branches=0,literal_islands=1,far_branches=0))
        self.assertEqual(e.finish(literal_islands=True),(code,relocs))

    def test_literal_island_does_not_split_a_call_delay_slot(self):
        e=Emitter(); e.constant(0,0x12345678)
        for _ in range(495): e.nop()
        e.word(0x410b); e.nop()
        for _ in range(105): e.nop()
        e.ret(); e.nop()  # match GNU text section's final four-byte alignment
        oracle=self.assemble('mov.l .Lvalue,r0\n.rept 495\nnop\n.endr\njsr @r1\nnop\n'
                             'bra .Lresume\nnop\n.align 2\n.Lvalue:\n.long 0x12345678\n'
                             '.Lresume:\n.rept 105\nnop\n.endr\nrts\nnop')
        self.assertEqual(e.finish(literal_islands=True)[0],oracle)

    def test_literal_islands_and_conditional_relaxation_share_the_final_layout(self):
        e=Emitter(); e.label('start'); e.branch('bt','end'); e.constant(0,0x12345678)
        for _ in range(600): e.nop()
        e.label('end'); e.branch('bf','start'); e.ret()
        oracle=self.assemble('.Lstart:\nbf .Lskip1\nbra .Lend\nnop\n.Lskip1:\nmov.l .Lvalue,r0\n'
                             '.rept 495\nnop\n.endr\nbra .Lresume\nnop\n.align 2\n.Lvalue:\n'
                             '.long 0x12345678\n.Lresume:\n.rept 105\nnop\n.endr\n.Lend:\n'
                             'bt .Lskip2\nbra .Lstart\nnop\n.Lskip2:\nrts\nnop')
        code,relocs,stats=e.finish_with_stats(relax=True,literal_islands=True)
        self.assertEqual(code,oracle); self.assertEqual(relocs,[])
        self.assertEqual(stats['expanded_conditional_branches'],2)
        self.assertEqual(stats['literal_islands'],1)
        self.assertEqual(e.instruction_size(relax=True,literal_islands=True),len(e.code)+12)

    def test_repeated_symbol_literals_have_distinct_relocations_in_separate_pools(self):
        e=Emitter(); e.literal(0,('symbol','external'))
        for _ in range(600): e.nop()
        e.literal(1,('symbol','external')); e.ret()
        oracle=self.assemble('mov.l .Lfirst,r0\n.rept 496\nnop\n.endr\nbra .Lresume\nnop\n'
                             '.align 2\n.Lfirst:\n.long _external\n.Lresume:\n.rept 104\nnop\n.endr\n'
                             'mov.l .Lsecond,r1\nrts\nnop\n.align 2\n.Lsecond:\n.long _external')
        code,relocs=e.finish(literal_islands=True)
        self.assertEqual(code,oracle)
        self.assertEqual(relocs,[dict(offset=1000,symbol='external'),dict(offset=1220,symbol='external')])

    def test_many_distinct_literals_split_into_bounded_pools(self):
        e=Emitter()
        for i in range(400): e.constant(0,0x10000000+i)
        e.ret()
        code,relocs,stats=e.finish_with_stats(literal_islands=True)
        self.assertFalse(relocs); self.assertGreater(stats['literal_islands'],1)
        # Independent disassembler verifies every decoded PC-relative operand.
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'many.bin'; path.write_bytes(code)
            objdump=BIN/('sh-elf-objdump'+EXE)
            result=subprocess.run([str(objdump),'-D','-b','binary','-m','sh4','-EL',str(path)],
                                  capture_output=True,text=True,check=True)
            addresses=[]
            for line in result.stdout.splitlines():
                match=re.match(r'\s*[0-9a-f]+:.*\bmov\.l\s+0x([0-9a-f]+),r0\b',line)
                if match: addresses.append(int(match[1],16))
            self.assertEqual(len(addresses),400)
            self.assertEqual([struct.unpack_from('<I',code,a)[0] for a in addresses],list(range(0x10000000,0x10000190)))

    def test_far_forward_branch_preserves_r0_with_a_relative_delta(self):
        e=Emitter(); e.branch('bra','end')
        for _ in range(2201): e.nop()
        e.label('end'); e.ret()
        oracle=self.assemble('mov.l r0,@-r15\nmov.l .Ldelta,r0\n.Ljump:\nbraf r0\nmov.l @r15+,r0\n'
                             '.align 2\n.Ldelta:\n.long .Lend-(.Ljump+4)\nnop\n.rept 2201\nnop\n.endr\n.Lend:\nrts\nnop')
        code,relocs,stats=e.finish_with_stats(far_branches=True)
        self.assertEqual(code,oracle); self.assertFalse(relocs); self.assertEqual(stats['far_branches'],1)
        with self.assertRaisesRegex(ValueError,'branch range'): e.finish()

    def test_far_backward_branch_matches_gnu_assembler(self):
        e=Emitter(); e.label('start')
        for _ in range(2201): e.nop()
        e.branch('bra','start'); e.ret(); e.nop()
        oracle=self.assemble('.Lstart:\n.rept 2201\nnop\n.endr\nmov.l r0,@-r15\nmov.l .Ldelta,r0\n'
                             '.Ljump:\nbraf r0\nmov.l @r15+,r0\n.align 2\n.Ldelta:\n'
                             '.long .Lstart-(.Ljump+4)\nnop\nrts\nnop\nnop')
        self.assertEqual(e.finish(far_branches=True)[0],oracle)

    def test_far_conditional_skips_the_entire_preserved_register_sequence(self):
        e=Emitter(); e.branch('bt','end')
        for _ in range(2200): e.nop()
        e.label('end'); e.ret()
        oracle=self.assemble('bf .Lskip\nmov.l r0,@-r15\nmov.l .Ldelta,r0\n.Ljump:\nbraf r0\n'
                             'mov.l @r15+,r0\n.align 2\n.Ldelta:\n.long .Lend-(.Ljump+4)\n'
                             '.Lskip:\n.rept 2200\nnop\n.endr\n.Lend:\nrts\nnop')
        code,_,stats=e.finish_with_stats(relax=True,far_branches=True)
        self.assertEqual(code,oracle); self.assertEqual(stats['expanded_conditional_branches'],1)
        self.assertEqual(stats['far_branches'],1)

    def test_far_branches_and_symbol_literal_islands_match_the_same_gnu_layout(self):
        e=Emitter(); e.literal(1,('symbol','external')); e.branch('bra','end')
        for _ in range(2200): e.nop()
        e.label('end'); e.literal(0,('symbol','external')); e.ret()
        oracle=self.assemble('mov.l .Lfirst,r1\nmov.l r0,@-r15\nmov.l .Ldelta,r0\n.Ljump:\nbraf r0\n'
                             'mov.l @r15+,r0\n.align 2\n.Ldelta:\n.long .Lend-(.Ljump+4)\nnop\n'
                             '.rept 488\nnop\n.endr\nbra .Lresume\nnop\n.align 2\n.Lfirst:\n.long _external\n'
                             '.Lresume:\n.rept 1712\nnop\n.endr\n.Lend:\nmov.l .Lsecond,r0\nrts\nnop\n'
                             '.align 2\n.Lsecond:\n.long _external')
        code,relocs,stats=e.finish_with_stats(literal_islands=True,far_branches=True)
        self.assertEqual(code,oracle)
        self.assertEqual(relocs,[dict(offset=1000,symbol='external'),dict(offset=4436,symbol='external')])
        self.assertEqual((stats['far_branches'],stats['literal_islands']),(1,1))

    def test_far_calls_and_changed_delay_slots_still_reject(self):
        for kind in ('bsr','bra'):
            e=Emitter(); e.branch(kind,'end')
            if kind=='bra': struct.pack_into('<H',e.code,2,0xe001)
            for _ in range(2200): e.nop()
            e.label('end'); e.ret()
            with self.subTest(kind=kind),self.assertRaisesRegex(ValueError,'branch range|NOP delay slot'):
                e.finish(far_branches=True)
        e=Emitter(); e.branch('bra','near'); e.nop(); e.label('near'); e.ret()
        self.assertEqual(e.finish(),e.finish(relax=True,literal_islands=True,far_branches=True))

    def test_islands_preserve_small_function_bytes_and_reject_literal_delay_slots(self):
        e=Emitter(); e.call_symbol('helper'); e.ret()
        self.assertEqual(e.finish(),e.finish(relax=True,literal_islands=True))
        e=Emitter(); e.word(0x410b); e.literal(0,('integer',0x12345678))
        for _ in range(600): e.nop()
        e.ret()
        with self.assertRaisesRegex(ValueError,'literal in a delay slot'):
            e.finish(literal_islands=True)

    def test_relaxation_repeats_when_one_expansion_pushes_another_out_of_range(self):
        e=Emitter(); e.branch('bt','near'); e.branch('bf','far')
        for _ in range(126): e.nop()
        e.label('near')
        for _ in range(130): e.nop()
        e.label('far'); e.ret()
        oracle=self.assemble('bf .Lskip1\nbra .Lnear\nnop\n.Lskip1:\nbt .Lskip2\n'
                             'bra .Lfar\nnop\n.Lskip2:\n.rept 126\nnop\n.endr\n.Lnear:\n'
                             '.rept 130\nnop\n.endr\n.Lfar:\nrts\nnop')
        self.assertEqual(e.finish(relax=True)[0],oracle)
        self.assertEqual(e.instruction_size(relax=True),len(e.code)+8)
        short=Emitter(); short.branch('bt','near'); short.nop(); short.label('near'); short.ret()
        self.assertEqual(short.finish(),short.finish(relax=True))

    def test_coalesced_integer_and_float_argument_leaves(self):
        for floating in (False,True):
            kind='float' if floating else 'int'
            fn={'name':'identity','return_type':kind,
                'registers':[{'id':0,'class':'float' if floating else 'gpr64','type':kind}],
                'parameters':[{'name':'value','register':0,'type':kind}],
                'instructions':[{'kind':'return','reads':[0],'writes':[]}]}
            source='rts\nfmov fr5,fr0' if floating else 'mov r4,r0\nrts\nmov r5,r1'
            self.assertEqual(compile_function(fn)[0],self.assemble(source))

    def assemble(self,source):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            asm,obj,binary=(root/name for name in ('oracle.s','oracle.o','oracle.bin'))
            asm.write_text('.text\n.little\n'+source+'\n')
            subprocess.run([str(ASSEMBLER),'-little',str(asm),'-o',str(obj)],check=True,capture_output=True)
            subprocess.run([str(OBJCOPY),'-O','binary','--only-section=.text',str(obj),str(binary)],check=True,capture_output=True)
            return binary.read_bytes()

    def test_integer_float_and_memory_encodings(self):
        sources={
          'add32':'mov r4,r0\nadd r5,r0\nrts\nnop',
          'add64':'clrt\nmov r4,r0\naddc r6,r0\nmov r5,r1\naddc r7,r1\nrts\nnop',
          'sub64':'clrt\nmov r4,r0\nsubc r6,r0\nmov r5,r1\nsubc r7,r1\nrts\nnop',
          'addfloat':'fmov fr4,fr0\nfadd fr5,fr0\nrts\nnop',
          'memory32':'mov.l r5,@r4\nmov.l @r4,r0\nrts\nnop',
        }
        objects=probes()
        for name,source in sources.items():
            with self.subTest(name=name): self.assertEqual(objects[name][0],self.assemble(source))

    def test_shift_halves_and_constant_sequences_against_gnu_assembler(self):
        for kind,count,source in [
            ('left',32,'mov r0,r1\nmov #0,r0'),
            ('logical',32,'mov r1,r0\nmov #0,r1'),
            ('arithmetic',32,'mov r1,r0\nmov #-31,r2\nshad r2,r1'),
            ('left',1,'mov #1,r2\nmov r0,r3\nmov r2,r4\nadd #-32,r4\nshld r4,r3\nshld r2,r1\nor r3,r1\nshld r2,r0')]:
            with self.subTest(kind=kind,count=count):
                e=Emitter(); emit_shift64(e,kind,count)
                self.assertEqual(e.finish()[0],self.assemble(source))

    def test_shift_leaf_coalescing_and_delay_slot_against_gnu_assembler(self):
        fn={'name':'shift','return_type':'int','registers':[{'id':0,'class':'gpr64','type':'int'}],
            'parameters':[{'register':0,'type':'int'}],
            'instructions':[{'kind':'shl64','shift':32,'reads':[0],'writes':[0]},
                            {'kind':'return','reads':[0],'writes':[]}]}
        for kind,source in [('shl64','mov #0,r0\nrts\nmov r4,r1'),
                            ('shr64','mov #0,r1\nrts\nmov r5,r0'),
                            ('sar64','mov r5,r0\nmov r5,r1\nmov #-31,r2\nrts\nshad r2,r1')]:
            fn['instructions'][0]['kind']=kind
            with self.subTest(kind=kind): self.assertEqual(compile_function(fn)[0],self.assemble(source))

    def test_restoring_division_instruction_encodings(self):
        e=Emitter(); e.word(0x4000); e.word(0x4324); e.rr(0x3002,3,2); e.word(0x4510)
        self.assertEqual(e.finish()[0],self.assemble('shll r0\nrotcl r3\ncmp/hs r2,r3\ndt r5'))

    def test_forward_backward_branches_and_delay_slots(self):
        sources={
          'choose32':'cmp/pz r4\nbf .Lneg\nmov r4,r0\nadd #5,r0\nrts\nnop\n.Lneg:\nneg r4,r0\nrts\nnop',
          'factorial32':'mov #1,r0\nmov #1,r1\n.Lloop:\ncmp/gt r1,r4\nbf .Lend\nmul.l r4,r0\nsts macl,r0\nadd #-1,r4\nbra .Lloop\nnop\n.Lend:\nrts\nnop',
        }
        for name,source in sources.items():
            with self.subTest(name=name): self.assertEqual(probes()[name][0],self.assemble(source))

    def test_pc_relative_alignment_and_literal_deduplication(self):
        e=Emitter(); e.nop(); e.constant(0,0x12345678); e.constant(1,0x12345678); e.ret()
        data,relocations=e.finish()
        oracle=self.assemble('nop\nmov.l .Lvalue,r0\nmov.l .Lvalue,r1\nrts\nnop\n.align 2\n.Lvalue:\n.long 0x12345678')
        self.assertEqual(data,oracle)
        self.assertEqual(relocations,[])
        self.assertEqual(data.count(struct.pack('<I',0x12345678)),1)

    def test_native_call_and_absolute_relocation_layout(self):
        oracle=self.assemble('sts.l pr,@-r15\nmov.l .Lbridge,r1\njsr @r1\nnop\nlds.l @r15+,pr\nadd #3,r0\nrts\nnop\n.align 2\n.Lbridge:\n.long _bridge')
        self.assertEqual(probes()['call_bridge'][0],oracle)

if __name__=='__main__': unittest.main()
