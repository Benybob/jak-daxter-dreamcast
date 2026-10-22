/* Registered alternate-stack calls with independent C/native observations.
 * Diagnostic only; no scheduler/suspension/unwinding. (AI-assisted), ISC. */
#include "goal_stack_call.h"
#include "goal_register_snapshot.h"
#include "goal_bridge.h"
#include "runtime_cases.h"
#include <string.h>
static struct {
 dc_goal_runtime *rt;uint32_t base,bytes,entry,calls,action,seed,snapshot_ok,nested_rc,nested_error;
 struct dc_goal_native_regs observed;
} probe;
static uint64_t callback(void) {
 uint64_t ignored=0x1111222233334444ull;
 ++probe.calls;probe.snapshot_ok=dc_goal_native_regs_snapshot(&probe.observed);
 if(probe.action) {
  probe.nested_rc=dc_goal_call_on_stack(probe.rt,probe.entry,probe.base,probe.bytes,&ignored);
  probe.nested_error=probe.rt->error;
 }
 /* Explicitly disturb caller-clobbered FPUL/status, without FP arithmetic. */
 __asm__ volatile("lds %0,fpul\n\tlds %1,fpscr" :: "r"(0x76543210u),"r"(0x0004007du) : "fpul","memory");
 return ((uint64_t)(probe.seed^0x89abcdefu)<<32)|(probe.seed^0x12345678u);
}
static float from_bits(uint32_t v) {float f;memcpy(&f,&v,4);return f;}
static uint32_t bits(float f) {uint32_t v;memcpy(&v,&f,4);return v;}
static int guarded(dc_goal_runtime *rt,uint32_t entry,uint32_t base,uint32_t bytes_,uint64_t *out,unsigned *intact) {
 register uint32_t a __asm__("r8")=0x12345678,b __asm__("r9")=0xfedcba98;
 register uint32_t c __asm__("r10")=0x31415926,d __asm__("r11")=0xabcdef01;
 register uint32_t e __asm__("r12")=0x10203040,f __asm__("r13")=0x87654321;
 register float x __asm__("fr12")=from_bits(0x7fc12345),y __asm__("fr13")=from_bits(0x80000000);
 register float z __asm__("fr14")=from_bits(1),w __asm__("fr15")=from_bits(0xffc54321);
 volatile uint32_t canary[4]={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
 uint32_t old_fpul,old_fpscr,new_fpul,new_fpscr;int result;
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1"
  : "=r"(old_fpul),"=r"(old_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),
    "+f"(x),"+f"(y),"+f"(z),"+f"(w) :: "memory");
 result=dc_goal_call_on_stack(rt,entry,base,bytes_,out);
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1"
  : "=r"(new_fpul),"=r"(new_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),
    "+f"(x),"+f"(y),"+f"(z),"+f"(w) :: "memory");
 *intact=a==0x12345678 && b==0xfedcba98 && c==0x31415926 && d==0xabcdef01 && e==0x10203040 && f==0x87654321 &&
  old_fpul==new_fpul && old_fpscr==new_fpscr && bits(x)==0x7fc12345 && bits(y)==0x80000000 && bits(z)==1 && bits(w)==0xffc54321 &&
  canary[0]==0x11223344 && canary[1]==0x55667788 && canary[2]==0x99aabbcc && canary[3]==0xddeeff00;
 return result;
}
static uint32_t entry(dc_goal_runtime *rt,const char *name) {return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
static uint32_t word(dc_goal_runtime *rt,uint32_t o,uint32_t off) {uint32_t v;memcpy(&v,dc_goal_resolve(rt,o+off,4),4);return v;}
static void put(dc_goal_runtime *rt,uint32_t o,uint32_t off,uint32_t v) {memcpy(dc_goal_resolve(rt,o+off,4),&v,4);}
static int filled(const void *p,size_t n) {const unsigned char *b=p;size_t i;for(i=0;i<n;++i)if(b[i]!=0xa5)return 0;return 1;}
uint32_t dc_runtime_stack_call_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync) {
 unsigned char cb_code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 unsigned char mode_code[7][14];
 const unsigned char mode_template[14]={0x38,0xe2,0x28,0x42,0x6a,0x42,0x5a,0xe0,0x3c,0xe1,0x0b,0,9,0};
 const char *names[7]={"dc-stack-mode-1","dc-stack-mode-2","dc-stack-mode-3","dc-stack-mode-4","dc-stack-mode-5","dc-stack-mode-6","dc-stack-mode-7"};
 dc_goal_code_export exports[8];dc_goal_module_result module;
 uint32_t target=(uint32_t)(uintptr_t)callback,allocation,base,i,code,proc_type,owner,frame,thread,old_object,old_process;
 uint64_t value,expected;unsigned intact;
 unsigned char header[128];
 typedef uint64_t(*two)(uint64_t,uint64_t);
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
#define GUARDS() do {CHECK(filled(dc_goal_resolve(rt,allocation,16),16),1,"alternate stack lower canary preserved");CHECK(filled(dc_goal_resolve(rt,base+16384,16),16),1,"alternate stack upper canary preserved");}while(0)
 memcpy(cb_code+8,&target,4);exports[0]=(dc_goal_code_export){"dc-stack-probe",cb_code,12,NULL,0};
 for(i=0;i<7;++i) {memcpy(mode_code[i],mode_template,14);mode_code[i][0]=(unsigned char)((i+1)<<3);exports[i+1]=(dc_goal_code_export){names[i],mode_code[i],14,NULL,0};}
 CHECK(dc_goal_load_module(rt,exports,8,sync,NULL,&module),1,"trusted diagnostic stack entries registered");
 if(!module.functions)return 0;
 code=entry(rt,"dc-stack-probe");CHECK(code!=0,1,"registered zero-argument scalar diagnostic entry");
 allocation=dc_goal_alloc(rt,16416);CHECK(allocation!=0,1,"owned16KiB alternate stack with separate guards");
 if(!allocation)return module.code_bytes;
 base=allocation+16;memset(dc_goal_resolve(rt,allocation,16416),0xa5,16416);
 memset(&probe,0,sizeof probe);probe.rt=rt;probe.base=base;probe.bytes=16384;probe.entry=code;
 for(i=0;i<32;++i) {
  probe.seed=i*0x13579bdfu;value=0;expected=((uint64_t)(probe.seed^0x89abcdefu)<<32)|(probe.seed^0x12345678u);
  CHECK(guarded(rt,code,base,16384,&value,&intact),1,"registered callback executes synchronously on owned alternate stack");
  CHECK(intact,1,"return restores original C GPR/FPR/FPUL/FPSCR/frame canaries");
  CHECK(value,expected,"both uint64 return halves preserved across stack transition");
  CHECK(probe.calls,i+1,"callback executes exactly once per request");CHECK(probe.snapshot_ok,1,"callback snapshots live native stack");
  CHECK(probe.observed.sp>=base && probe.observed.sp<base+16384,1,"observed callback SP lies inside owned alternate span");
  CHECK(probe.observed.sp&3,0,"callback SP meets four-byte ABI alignment");
  CHECK(probe.observed.pr!=0 && !(probe.observed.pr&1),1,"callback snapshot has an aligned real return site");
  GUARDS();CHECK(rt->error,DC_GOAL_OK,"successful alternate-stack call preserves clear runtime status");
 }
 for(i=0;i<7;++i) {
  value=0;CHECK(guarded(rt,entry(rt,names[i]),base,16384,&value,&intact),1,"raw return tolerates each target FR/SZ/PR mode combination");
  CHECK(intact,1,"epilogue restores default bank before original FPRs and exact original status");
  CHECK(value,0x0000003c0000005aull,"mode-changing target preserves both scalar result halves");
  CHECK(rt->error,DC_GOAL_OK,"mode-changing target returns without runtime error");GUARDS();
 }
 for(i=0;i<12;++i) {
  uint32_t address=i==0?0:(i==1?rt->s7:(i==2?code+2:code));
  uint32_t stack=i==5?base+1:(i==7?0xfffffff0u:(i==8?rt->base+rt->used:(i==9?code&~15u:base)));
  uint32_t size=i==3?0:(i==4?4080:(i==6?16383:16384));
  uint64_t *out=i==10?dc_goal_resolve(rt,base+32,8):(i==11?NULL:&value);
  value=0x1122334455667788ull;
  CHECK(dc_goal_call_on_stack(rt,address,stack,size,out),0,"invalid entry/span/output rejected before target execution");
  CHECK(probe.calls,32,"rejected request never invokes diagnostic callback");CHECK(value,0x1122334455667788ull,"rejected call preserves separate caller output");GUARDS();
 }
 probe.action=1;value=0x1122334455667788ull;
 CHECK(guarded(rt,code,base,16384,&value,&intact),0,"nested stack-call rejection propagates error after normal return");
 CHECK(intact,1,"nested rejection still restores original C state");CHECK(probe.nested_rc,0,"reentrant API call rejected");
 CHECK(probe.nested_error,DC_GOAL_BAD_ARGUMENT,"reentrant API error explicit");
 CHECK(value,0x1122334455667788ull,"callee error leaves output untouched");CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"callee error not silently replaced by success");GUARDS();
 probe.action=0;
 CHECK(guarded(rt,code,base,16384,&value,&intact),1,"a deliberate later diagnostic call can run after normal error return");
 CHECK(intact,1,"later call restores C state");CHECK(probe.calls,34,"nested attempt did not execute a second callback");
 /* Execute the unchanged original read-only-self body on the owned CPU stack. */
 proc_type=entry(rt,"process");old_object=dc_goal_get_method(rt,rt->object_type,0);old_process=dc_goal_get_method(rt,proc_type,0);
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"explicit sized object allocation selected");
 CHECK(dc_goal_set_method(rt,proc_type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 dc_goal_clear_process_context(rt);
 owner=(uint32_t)((two)(uintptr_t)entry(rt,"dc-new-process"))(dc_goal_intern(rt,"dc-stack-owner"),4096);
 CHECK(owner!=0,1,"original constructor owns the bounded process reservation");
 frame=dc_goal_new_basic(rt,entry(rt,"stack-frame"),12);CHECK(frame!=0,1,"original stack-frame data allocated");
 if(!owner || !frame)return module.code_bytes;
 thread=dc_goal_construct_main_thread(rt,owner,4208,base,16384,dc_goal_intern(rt,"dc-stack-main"),0);
 CHECK(thread!=0,1,"original CPU-thread constructor owns the same stack top");
 if(!thread)return module.code_bytes;
 memcpy(header,dc_goal_resolve(rt,thread-4,128),128);put(rt,frame,4,rt->s7);put(rt,owner,88,frame);
 value=0x1122334455667788ull;
 CHECK(dc_goal_call_on_stack(rt,entry(rt,"remove-exit"),base,16384,&value),0,"original implicit-self entry still requires explicit execution context");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"missing context fails closed on alternate stack");
 CHECK(value,0x1122334455667788ull,"missing context leaves caller output untouched");CHECK(word(rt,owner,88),frame,"missing context never accesses original frame field");
 CHECK(dc_goal_bind_process_context(rt,owner,4208),1,"execution identity explicitly bound before original behavior");
 CHECK(guarded(rt,entry(rt,"remove-exit"),base,16384,&value,&intact),1,"unchanged original remove-exit executes on CPU-owned alternate stack");
 CHECK(value,rt->s7,"original behavior returns next-frame false");CHECK(word(rt,owner,88),rt->s7,"original behavior pops its original frame");
 CHECK(memcmp(header,dc_goal_resolve(rt,thread-4,128),128),0,"diagnostic call never rewrites original saved CPU metadata");
 CHECK(word(rt,owner,32),dc_goal_find(rt,"dead"),"diagnostic does not infer process activation");CHECK(word(rt,owner,40),rt->s7,"diagnostic does not install main-thread");
 GUARDS();CHECK(intact,1,"original behavior return restores C caller");
 dc_goal_clear_process_context(rt);CHECK(word(rt,entry(rt,"*kernel-context*"),20),rt->s7,"explicit context cleanup restores original current-process false");
 dc_goal_set_method(rt,rt->object_type,0,old_object);dc_goal_set_method(rt,proc_type,0,old_process);dc_goal_clear_process_heap(rt);
#undef GUARDS
#undef CHECK
 return module.code_bytes;
}
