/* Native closed-site escape, independent GCC/stack oracle (AI-assisted), ISC. */
#include "goal_catch_raw.h"
#include "runtime_cases.h"
#include <string.h>

extern void dc_goal_catch_clobber_escape(dc_goal_catch_raw *) __attribute__((noreturn));
typedef struct {
 dc_goal_catch_raw *outer;
 dc_goal_catch_raw *inner;
 uint64_t value;
 unsigned path;
 volatile unsigned stage,inner_returned;
} invocation;
static float from_bits(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static uint32_t bits(float f){uint32_t x;memcpy(&x,&f,4);return x;}
static uint64_t normal(void *v){invocation *i=v;i->stage=1;return i->value;}
static uint64_t escape_deep(void *v) __attribute__((noinline));
static uint64_t escape_deep(void *v){
 invocation *i=v;volatile uint32_t local[32];unsigned n;
 for(n=0;n<32;++n)local[n]=n^0xaabbccddu;
 i->stage=2+(local[31]!= (31^0xaabbccddu));
 dc_goal_catch_clobber_escape(i->outer);
}
static uint64_t inner(void *v){
 invocation *i=v;
 if(i->path==3)return escape_deep(v); /* skip this entire nested invocation */
 if(i->path==4){i->stage=3;dc_goal_catch_clobber_escape(i->inner);}
 return normal(v);
}
static uint64_t nested(void *v){
 invocation *i=v;dc_goal_catch_raw child;uint64_t result;
 memset(&child,0,sizeof child);
 child.result=i->value^UINT64_C(0x8f7e6d5c4b3a2910);i->inner=&child;
 result=dc_goal_catch_invoke_raw(&child,inner,i);
 i->inner_returned=1;
 if(i->path==4){i->stage=4;return result+1;}
 return result;
}

static unsigned guarded(dc_goal_catch_raw *frame,invocation *i,uint32_t mode,
 uint64_t *result,uint32_t *sp){
 register uint32_t a __asm__("r8")=0x10203040,b __asm__("r9")=0xaabbccdd;
 register uint32_t c __asm__("r10")=0x31415926,d __asm__("r11")=0xabcdef01;
 register uint32_t e __asm__("r12")=0x76543210,f __asm__("r13")=0xfedcba98;
 register float x __asm__("fr12")=from_bits(0x7fc12345),y __asm__("fr13")=from_bits(0x80000000);
 register float z __asm__("fr14")=from_bits(1),w __asm__("fr15")=from_bits(0xbf800000);
 volatile uint32_t guards[4]={0x11112222,0x33334444,0x55556666,0x77778888};
 uint32_t old_fpul,old_fpscr,after_fpul,after_fpscr,before_sp,after_sp;
 unsigned valid;uint32_t pattern=0x87654321;
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1" : "=r"(old_fpul),"=r"(old_fpscr));
 __asm__ volatile("lds %0,fpul\n\tlds %1,fpscr\n\tmov r15,%2"
  : "+r"(pattern),"+r"(mode),"=r"(before_sp),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),
    "+f"(x),"+f"(y),"+f"(z),"+f"(w) :: "memory");
 *result=dc_goal_catch_invoke_raw(frame,i->path==0?normal:i->path==1?escape_deep:nested,i);
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1\n\tmov r15,%2"
  : "=r"(after_fpul),"=r"(after_fpscr),"=r"(after_sp),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),
    "+f"(x),"+f"(y),"+f"(z),"+f"(w) :: "memory");
 sp[0]=before_sp;sp[1]=after_sp;
 valid=a==0x10203040 && b==0xaabbccdd && c==0x31415926 && d==0xabcdef01 &&
 e==0x76543210 && f==0xfedcba98 && after_fpul==0x87654321 && after_fpscr==mode &&
 bits(x)==0x7fc12345 && bits(y)==0x80000000 && bits(z)==1 && bits(w)==0xbf800000 &&
 guards[0]==0x11112222 && guards[1]==0x33334444 && guards[2]==0x55556666 && guards[3]==0x77778888;
 __asm__ volatile("lds %0,fpul\n\tlds %1,fpscr\n\tnop" :: "r"(old_fpul),"r"(old_fpscr) : "memory");
 return valid;
}
void dc_runtime_catch_raw_cases(dc_runtime_check check,void *context,unsigned *id){
 static const uint64_t values[]={0,UINT64_C(0x100000000),UINT64_C(0xfedcba9876543210),UINT64_MAX};
 static const uint32_t modes[]={0,0x40000,1,0x4007d};
 struct {uint32_t low[4];dc_goal_catch_raw frame;uint32_t high[4];} box;
 unsigned m,v,path,n,intact;invocation call;uint64_t result;uint32_t sp[2];
#define CHECK(a,b,s) check((uint64_t)(a),(uint64_t)(b),++*id,s,context)
 CHECK(sizeof(dc_goal_catch_raw),72,"raw catch frame layout measured by installed SH4 GCC");
 CHECK(offsetof(dc_goal_catch_raw,result),64,"escape result occupies separately bounded exact offset");
 for(m=0;m<4;++m)for(v=0;v<4;++v)for(path=0;path<5;++path){
  memset(&box,0xa5,sizeof box);memset(&call,0,sizeof call);
  call.outer=&box.frame;call.value=values[v];call.path=path;
  box.frame.result=(path==1||path==3)?values[v]:~values[v];result=0xdead;
  CHECK(guarded(&box.frame,&call,modes[m],&result,sp),1,"normal/escape restores GCC GPR/FP/FPUL/FPSCR and root stack canaries");
  CHECK(result,path==4?(values[v]^UINT64_C(0x8f7e6d5c4b3a2910))+1:values[v],"normal and nonlocal return preserve every result bit including zero");
  CHECK(call.stage,path==4?4:(path==1||path==3)?2:1,"callback reached and throw skipped ordinary C returns");
  CHECK(sp[0],sp[1],"invoke returns exactly once at original native SP");
  CHECK(box.frame.regs.sp,sp[0]-4,"saved SP is exact wrapper PR word below original C caller SP");
  CHECK(box.frame.regs.pr,(uintptr_t)dc_goal_catch_invoke_return,"captured continuation is exact closed wrapper label");
  CHECK(box.frame.reserved,0xa5a5a5a5,"raw save keeps padding outside register image untouched");
  intact=1;for(n=0;n<4;++n)if(box.low[n]!=0xa5a5a5a5 || box.high[n]!=0xa5a5a5a5)intact=0;
  CHECK(intact,1,"exact saved context and result keep both frame boundary canaries intact");
  CHECK(call.inner_returned,path==2||path==4,"inner escape resumes outer callback; outer escape discards nested invocation");
  CHECK(box.frame.result,(path==1||path==3)?values[v]:~values[v],"normal result differs from escape slot and save leaves it untouched");
 }
#undef CHECK
}
