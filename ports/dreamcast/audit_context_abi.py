"""Measure installed SH4 compiler/SDK context assumptions without execution (AI-assisted).

Requires a KOS-configured sh-elf-gcc with SDK headers. Writes only under --output.
This compiles assembly; it does not link a program or validate a context switch.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

FLAGS=['-O2','-fno-builtin','-m4-single','-ml','-mfsrra','-mfsca',
       '-ffunction-sections','-fdata-sections','-matomic-model=soft-imask',
       '-ftls-model=local-exec']
CLOBBER=r'''
/* Compiler clobber declarations only; never executed (AI-assisted). */
__attribute__((noinline,used)) void clobber_gpr(void) {
 __asm__ volatile("" ::: "r8","r9","r10","r11","r12","r13","r14");
}
__attribute__((noinline,used)) void clobber_fpr(void) {
 __asm__ volatile("" ::: "fr12","fr13","fr14","fr15");
}
__attribute__((noinline,used)) void clobber_special(void) {
 __asm__ volatile("" ::: "pr","fpul","mach","macl");
}
extern unsigned long long target64(unsigned long long,unsigned long long,unsigned long long);
__attribute__((noinline,used)) unsigned long long forward64(unsigned long long a) {
 unsigned long long result=target64(a,0x1122334455667788ull,0x99aabbccddeeff00ull);
 __asm__ volatile("" : "+r"(result)); return result;
}
extern float target_float(float,float,float,float,float,float,float,float,float);
__attribute__((noinline,used)) float forward_float(float a) {
 float result=target_float(a,2.f,3.f,4.f,5.f,6.f,7.f,8.f,9.f);
 __asm__ volatile("" : "+f"(result)); return result;
}
'''
LAYOUT=r'''
/* SDK layout constants, not an executable context (AI-assisted). */
#include <stddef.h>
#include <setjmp.h>
#include <arch/irq.h>
const unsigned context_layout[] = {
 sizeof(irq_context_t), __alignof__(irq_context_t), sizeof(jmp_buf),
 offsetof(irq_context_t,pc),offsetof(irq_context_t,pr),
 offsetof(irq_context_t,fpul),offsetof(irq_context_t,fr),
 offsetof(irq_context_t,frbank),offsetof(irq_context_t,r),
 offsetof(irq_context_t,fpscr)
};
'''

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--cc',default='sh-elf-gcc')
 parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args()
 out=args.output.resolve()
 out.mkdir(parents=True,exist_ok=False)
 def command(argv):
  result=subprocess.run(argv,capture_output=True,text=True)
  return result
 version=command([args.cc,'-v'])
 (out/'compiler-version.log').write_text(version.stdout+version.stderr,encoding='utf-8')
 for name,source in [('clobber',CLOBBER),('layout',LAYOUT)]:
  (out/(name+'.c')).write_text(source,encoding='utf-8')
  result=command([args.cc,*FLAGS,'-S',str(out/(name+'.c')),'-o',str(out/(name+'.s'))])
  (out/(name+'-stderr.log')).write_text(result.stderr,encoding='utf-8')
  if result.returncode: raise RuntimeError(name+': '+result.stderr)
 target=command([args.cc,*FLAGS,'-Q','--help=target'])
 if target.returncode: raise RuntimeError(target.stderr)
 (out/'compiler-target.log').write_text(target.stdout+target.stderr,encoding='utf-8')
 assembly=(out/'clobber.s').read_text()
 def body(name):
  match=re.search(r'^_'+name+r':\n(.*?)^\s*\.size\s+_'+name+r',',assembly,re.M|re.S)
  if not match: raise RuntimeError('unsupported assembler function layout: '+name)
  return match.group(1)
 gpr=body('clobber_gpr'); fpr=body('clobber_fpr'); special=body('clobber_special')
 checks={}
 for n in range(8,15):
  checks['save_r'+str(n)]=bool(re.search(r'mov\.l\s+r'+str(n)+r',@-r15',gpr))
  checks['restore_r'+str(n)]=bool(re.search(r'mov\.l\s+@r15\+,r'+str(n)+r'\b',gpr))
 for n in range(12,16):
  checks['save_fr'+str(n)]=bool(re.search(r'fmov\.s\s+fr'+str(n)+r',@-r15',fpr))
  checks['restore_fr'+str(n)]=bool(re.search(r'fmov\.s\s+@r15\+,fr'+str(n)+r'\b',fpr))
 checks['save_pr']=bool(re.search(r'sts\.l\s+pr,@-r15',special))
 checks['restore_pr']=bool(re.search(r'lds\.l\s+@r15\+,pr',special))
 if not all(checks.values()): raise RuntimeError('compiler preservation observation changed: '+str(checks))
 values=[int(v) for v in re.findall(r'^\s*\.long\s+(\d+)\s*$',(out/'layout.s').read_text(),re.M)]
 fields=['irq_bytes','irq_alignment','jmp_buf_bytes','pc','pr','fpul','fr','frbank','r','fpscr']
 if len(values)!=len(fields): raise RuntimeError('unsupported SDK layout assembly')
 report={'ai_assisted':True,'scope':'compiler assembly and SDK layout only; no execution or context switch',
         'flags':FLAGS,'preservation_checks':checks,'sdk_layout':dict(zip(fields,values)),
         'fpul_mach_macl_saved_by_special_probe':bool(re.search(r'sts\.l\s+(?:fpul|mach|macl),',special)),
         'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if p.is_file()}}
 (out/'observations.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'result':'PASS','preservation_checks':len(checks),'sdk_layout':report['sdk_layout']}))

if __name__=='__main__': main()
