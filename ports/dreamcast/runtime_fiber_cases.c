/* Two original CPU backups sharing a native working stack (AI-assisted), ISC. */
#include "goal_fiber.h"
#include "fiber_test_support.h"
#include "fiber_bounds_cases.h"
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/fiber_fixture.h"
#include <string.h>
extern const unsigned char dc_goal_context_leave_return[],dc_goal_context_boot_raw[];
static uint32_t entry(dc_goal_runtime *rt,const char *name){return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
static uint32_t word(dc_goal_runtime *rt,uint32_t o,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(rt,o+off,4),4);return v;}
static int filled_v(const void *p,size_t n,unsigned char v){const unsigned char *b=p;size_t i;for(i=0;i<n;++i)if(b[i]!=v)return 0;return 1;}
static int filled(const void *p,size_t n){return filled_v(p,n,0xa5);}
static float from_bits(uint32_t v){float f;memcpy(&f,&v,4);return f;}
static uint32_t bits(float f){uint32_t v;memcpy(&v,&f,4);return v;}
static int guarded_step(dc_goal_fiber *fiber,uint64_t send,uint64_t *out,unsigned *intact){
 register uint32_t a __asm__("r8")=0x12345678,b __asm__("r9")=0xfedcba98;
 register uint32_t c __asm__("r10")=0x31415926,d __asm__("r11")=0xabcdef01;
 register uint32_t e __asm__("r12")=0x10203040,f __asm__("r13")=0x87654321;
 register float x __asm__("fr12")=from_bits(0x7fc12345),y __asm__("fr13")=from_bits(0x80000000);
 register float z __asm__("fr14")=from_bits(1),w __asm__("fr15")=from_bits(0xffc54321);
 volatile uint32_t canary[4]={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
 uint32_t old_fpul,old_fpscr,new_fpul,new_fpscr;int result;
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1"
  :"=r"(old_fpul),"=r"(old_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),"+f"(x),"+f"(y),"+f"(z),"+f"(w)::"memory");
 result=dc_goal_fiber_step(fiber,send,out);
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1"
  :"=r"(new_fpul),"=r"(new_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),"+f"(x),"+f"(y),"+f"(z),"+f"(w)::"memory");
 *intact=a==0x12345678 && b==0xfedcba98 && c==0x31415926 && d==0xabcdef01 && e==0x10203040 && f==0x87654321 &&
  old_fpul==new_fpul && old_fpscr==new_fpscr && bits(x)==0x7fc12345 && bits(y)==0x80000000 && bits(z)==1 && bits(w)==0xffc54321 &&
  canary[0]==0x11223344 && canary[1]==0x55667788 && canary[2]==0x99aabbcc && canary[3]==0xddeeff00;
 return result;
}
int dc_runtime_fiber_checked_step(dc_goal_fiber *fiber,uint64_t send,uint64_t *out,unsigned *intact){
 return guarded_step(fiber,send,out,intact);
}
/* Alternate root depths so a previous step's stack anchor cannot be reused. */
__attribute__((noinline)) static int deep_step(dc_goal_fiber *fiber,uint64_t send,uint64_t *out,unsigned *intact){
 volatile uint32_t pad[128];unsigned i;int event;
 for(i=0;i<128;++i)pad[i]=0x91827364u^i;
 event=guarded_step(fiber,send,out,intact);
 for(i=0;i<128;++i)if(pad[i]!=(0x91827364u^i))*intact=0;
 return event;
}
static unsigned native_action,native_rejected;
static uint64_t native_body(void){
 dc_goal_fiber *fiber=dc_goal_fiber_current();uint64_t value,ignored=0;
 uint32_t before_fpul,before_fpscr=0x0004007d,after_fpul,after_fpscr;
 register uint32_t a __asm__("r8")=0x98765432,b __asm__("r9")=0xabcdef01;
 register uint32_t c __asm__("r10")=0x76543210,d __asm__("r11")=0x13579bdf;
 register uint32_t e __asm__("r12")=0xfdb97531,f __asm__("r13")=0x10203040;
 register float x __asm__("fr12")=from_bits(0xffc12345),y __asm__("fr13")=from_bits(0x80000001);
 register float z __asm__("fr14")=from_bits(0x80000000),w __asm__("fr15")=from_bits(0x7fc54321);
 if(native_action){
  if(native_action==1)native_rejected=dc_goal_fiber_clear(fiber);
  if(native_action==2)native_rejected=dc_goal_fiber_init(fiber,fiber->runtime,fiber->process,fiber->reservation,fiber->thread,fiber->stack_base,fiber->stack_bytes,fiber->entry);
  if(native_action==3)native_rejected=dc_goal_fiber_step(fiber,0,&ignored);
  if(native_action==4)native_rejected=(unsigned)dc_goal_fiber_yield(NULL,0);
  if(native_action==5)native_rejected=dc_goal_activate_process(fiber->runtime,0,0,0,0,0,0);
  if(native_action==6)native_rejected=(unsigned)dc_goal_fiber_init_words(fiber,fiber->runtime,fiber->process,fiber->reservation,fiber->thread,fiber->stack_base,fiber->stack_bytes,fiber->entry,9,(const uint64_t *)(uintptr_t)1);
  return 0x12345678;
 }
 before_fpul=0x55aa33ccu^fiber->process;
 __asm__ volatile("lds %0,fpul\n\tlds %1,fpscr"::"r"(before_fpul),"r"(before_fpscr):"fpul","memory");
 __asm__ volatile("":"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),"+f"(x),"+f"(y),"+f"(z),"+f"(w)::"memory");
 value=dc_goal_fiber_yield(fiber->runtime,0xabc0000000000000ull|fiber->process);
 __asm__ volatile("sts fpul,%0\n\tsts fpscr,%1":"=r"(after_fpul),"=r"(after_fpscr),"+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),"+f"(x),"+f"(y),"+f"(z),"+f"(w)::"memory");
 if(a!=0x98765432 || b!=0xabcdef01 || c!=0x76543210 || d!=0x13579bdf || e!=0xfdb97531 || f!=0x10203040 ||
    before_fpul!=after_fpul || before_fpscr!=after_fpscr || bits(x)!=0xffc12345 || bits(y)!=0x80000001 || bits(z)!=0x80000000 || bits(w)!=0x7fc54321)return ~value;
 return value;
}
uint32_t dc_runtime_fiber_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync){
 dc_goal_fiber fibers[2];dc_goal_module_result bridge,module;
 uint32_t owners[2],threads[2],frames[2][3],allocation,base,body,proc_type,old_object,old_process,used,symbols,round,i;
 unsigned intact;uint64_t value;
 dc_goal_module_result native_module;uint32_t native_entry,j,symbol,old_value;
 unsigned char backup_before[32],native_code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export native_export={"dc-fiber-native-body",native_code,12,NULL,0};
 const uint64_t expected[2][4]={{11,104,110,115},{11,113,137,169}};
 unsigned char header[2][128];typedef uint64_t(*two)(uint64_t,uint64_t);
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 dc_fiber_bounds_cases(check,context,id);
 CHECK(dc_goal_fiber_yield(rt,7),0,"yield outside an active scope has no fallback");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"missing yield scope error explicit");
 CHECK(dc_goal_install_fiber_bridge(rt,sync,&bridge),1,"explicit cooperative yield bridge installed in active runtime");
 CHECK(dc_goal_set_symbol(rt,dc_goal_intern(rt,"dc-fiber-pop"),entry(rt,"remove-exit")),1,"unchanged original behavior is imported through an explicit scalar alias");
 CHECK(dc_goal_load_module(rt,goal_fiber_fixture_exports,GOAL_FIBER_FIXTURE_EXPORT_COUNT,sync,NULL,&module),1,"native GOAL nested-yield fixture links");
 if(!bridge.functions || !module.functions)return bridge.code_bytes;
 CHECK(module.functions,2,"two native GOAL bodies exercise nested call frames");
 allocation=dc_goal_alloc(rt,16416);CHECK(allocation!=0,1,"two contexts share one owned16KiB working stack");
 if(!allocation)return bridge.code_bytes+module.code_bytes;
 base=allocation+16;memset(dc_goal_resolve(rt,allocation,16416),0xa5,16416);
 body=entry(rt,"dc-fiber-main");CHECK(body!=0,1,"current exact scalar body entry selected");
 proc_type=entry(rt,"process");old_object=dc_goal_get_method(rt,rt->object_type,0);old_process=dc_goal_get_method(rt,proc_type,0);
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"explicit sized allocation selected");
 CHECK(dc_goal_set_method(rt,proc_type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 dc_goal_clear_process_context(rt);
 for(i=0;i<2;++i){
  owners[i]=(uint32_t)((two)(uintptr_t)entry(rt,"dc-new-process"))(dc_goal_intern(rt,i?"dc-fiber-owner-b":"dc-fiber-owner-a"),12288);
  CHECK(owners[i]!=0,1,"original process owns private backup reservation");if(!owners[i])return bridge.code_bytes+module.code_bytes;
  threads[i]=dc_goal_construct_main_thread(rt,owners[i],12400,base,16384,dc_goal_intern(rt,i?"dc-fiber-thread-b":"dc-fiber-thread-a"),8192);
  CHECK(threads[i]!=0,1,"original CPU-thread owns separate8192-byte backup payload");if(!threads[i])return bridge.code_bytes+module.code_bytes;
  memcpy(header[i],dc_goal_resolve(rt,threads[i]-4,128),128);
  CHECK(dc_goal_fiber_init(&fibers[i],rt,owners[i],12400,threads[i],base,16384,body),1,"native context metadata initialized outside working stack");
  CHECK(fibers[i].phase,DC_FIBER_NEW,"new context has not executed");CHECK(fibers[i].image.backup_bytes,8192,"explicit original backup capacity retained");
  for(j=0;j<3;++j){
   uint32_t next=j?frames[i][j-1]:rt->s7;
   frames[i][j]=dc_goal_new_basic(rt,entry(rt,"stack-frame"),12);
   CHECK(frames[i][j]!=0,1,"three original frame objects belong to each diagnostic process");
   if(!frames[i][j])return bridge.code_bytes+module.code_bytes;
   memcpy(dc_goal_resolve(rt,frames[i][j]+4,4),&next,4);
  }
  memcpy(dc_goal_resolve(rt,owners[i]+88,4),&frames[i][2],4);
 }
 used=rt->used;symbols=rt->symbol_count;
 for(round=0;round<4;++round)for(i=0;i<2;++i){
  CHECK(dc_goal_bind_process_context(rt,owners[i],12400),1,"each step binds execution identity explicitly");
  value=0x1122334455667788ull;
  CHECK((round&1?deep_step:guarded_step)(&fibers[i],round*(i?10u:1u),&value,&intact),round<3?DC_FIBER_YIELDED:DC_FIBER_COMPLETED,"GOAL context yields/resumes/completes through exact native continuation sites");
  CHECK(intact,1,"root C caller GPR/FPR/status/frame preserved across coroutine step");
  CHECK(value,expected[i][round],"independent GOAL local/send arithmetic across nested suspended frames");
  CHECK(word(rt,owners[i],88),round<2?frames[i][1-round]:rt->s7,"unchanged original remove-exit observes the explicitly rebound process across every resume");
  CHECK(rt->error,DC_GOAL_OK,"successful cooperative step has clear runtime status");
  CHECK(fibers[i].phase,round<3?DC_FIBER_SUSPENDED:DC_FIBER_DONE,"context lifecycle matches observed event");
  CHECK(dc_goal_fiber_current()==NULL,1,"root has no active fiber after return");CHECK(fibers[i].root_live,0,"returned root anchor is no longer live");
  if(round<3){
   CHECK(fibers[i].image.saved,1,"suspended context has one exact backup image");
   CHECK(fibers[i].native_thread.sp,fibers[i].image.image_sp,"saved native SP matches image range");
   CHECK(fibers[i].image.image_bytes>0 && fibers[i].image.image_bytes<=8192,1,"native saved frame depth fits owned payload");
   CHECK(fibers[i].native_thread.pr,(uint32_t)(uintptr_t)dc_goal_context_leave_return,"paused native PC is exact leave-shim continuation");
  }else{
   CHECK(fibers[i].image.saved,0,"completed context has no pending restoration");
   value=0x1122334455667788ull;CHECK(dc_goal_fiber_step(&fibers[i],0,&value),0,"completed context cannot re-enter bootstrap or completed frame");
   CHECK(fibers[i].phase,DC_FIBER_DONE,"terminal rejection does not resurrect context");
   CHECK(value,0x1122334455667788ull,"terminal rejection preserves caller output");
  }
  CHECK(filled(dc_goal_resolve(rt,allocation,16),16),1,"shared working stack low guard survives");
  CHECK(filled(dc_goal_resolve(rt,base+16384,16),16),1,"shared working stack high guard survives");
  CHECK(memcmp(header[i],dc_goal_resolve(rt,threads[i]-4,128),128),0,"cooperative engine preserves originalCPU PC/SP/register header");
  dc_goal_clear_process_context(rt);CHECK(word(rt,entry(rt,"*kernel-context*"),20),rt->s7,"root explicitly releases process execution identity");
  /* Destroy the entire execution image. Only private CPU backup can recover it. */
  memset(dc_goal_resolve(rt,base,16384),0xd3,16384);
 }
 CHECK(rt->used,used,"GOAL cooperative steps allocate no arena bytes");
 CHECK(rt->symbol_count,symbols,"GOAL cooperative steps intern no names");
 /* Independent native C bodies keep actual callee GPR/FPR/control values live. */
 {uint32_t target=(uint32_t)(uintptr_t)native_body;memcpy(native_code+8,&target,4);}
 CHECK(dc_goal_load_module(rt,&native_export,1,sync,NULL,&native_module),1,"independent native bit-state body registered");
 native_entry=entry(rt,"dc-fiber-native-body");used=rt->used;symbols=rt->symbol_count;
 CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"fresh context for state-qualified continuation test");
 fibers[0].native_thread.pr=(uint32_t)(uintptr_t)dc_goal_context_leave_return;value=0x1122334455667788ull;
 CHECK(dc_goal_fiber_step(&fibers[0],0,&value),0,"NEW context cannot use an otherwise-allowed suspended continuation");
 CHECK(fibers[0].phase,DC_FIBER_FAULT,"wrong-phase native PC closes context");
 CHECK(value,0x1122334455667788ull,"wrong-phase native PC preserves output");
 CHECK(fibers[0].image.valid,0,"wrong-phase context drops restoration authority");
 CHECK(filled_v(dc_goal_resolve(rt,base,16384),16384,0xd3),1,"wrong-phase PC checked before using working stack");
 for(i=0;i<2;++i)CHECK(dc_goal_fiber_init(&fibers[i],rt,owners[i],12400,threads[i],base,16384,native_entry),1,"native state context reinitialized explicitly");
 for(round=0;round<2;++round)for(i=0;i<2;++i){
  uint64_t send=i?0xfedcba9876543210ull:0x1020304050607080ull;
  CHECK(dc_goal_bind_process_context(rt,owners[i],12400),1,"native state owner explicitly bound");
  CHECK((round&1?deep_step:guarded_step)(&fibers[i],send,&value,&intact),round?DC_FIBER_COMPLETED:DC_FIBER_YIELDED,"native state body suspends and resumes exactly once");
  CHECK(intact,1,"root registers/control restored independently of native fiber patterns");
  CHECK(value,round?send:(0xabc0000000000000ull|owners[i]),"fiber GPR/FPR/FPUL/full FPSCR and64-bit resumed data remain bit-exact");
  CHECK(rt->error,DC_GOAL_OK,"native state body has no mismatch/error");
  CHECK(filled(dc_goal_resolve(rt,allocation,16),16),1,"native state lower stack fence");
  CHECK(filled(dc_goal_resolve(rt,base+16384,16),16),1,"native state upper stack fence");
  dc_goal_clear_process_context(rt);memset(dc_goal_resolve(rt,base,16384),0xd3,16384);
 }
 /* Invalid output is a retryable call error, not a corrupted continuation. */
 CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"fresh pending context for output validation");
 CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"output probe explicit owner");
 CHECK(dc_goal_fiber_step(&fibers[0],0,&value),DC_FIBER_YIELDED,"output probe has a real suspended image");
 memset(dc_goal_resolve(rt,base,16384),0xd3,16384);
 CHECK(dc_goal_fiber_step(&fibers[0],0,&fibers[0].value),0,"output may not alias context metadata");
 CHECK(fibers[0].phase,DC_FIBER_SUSPENDED,"bad output preserves paused lifecycle");
 CHECK(fibers[0].image.saved,1,"bad output does not consume backup image");
 CHECK(fibers[0].value,0xabc0000000000000ull|owners[0],"bad output preserves yielded data");
 CHECK(dc_goal_fiber_step(&fibers[0],0,dc_goal_resolve(rt,base+32,8)),0,"output may not alias managed working stack");
 CHECK(filled_v(dc_goal_resolve(rt,base,16384),16384,0xd3),1,"bad output rejects before stack restoration");
 CHECK(dc_goal_fiber_step(&fibers[0],0x6677,&value),DC_FIBER_COMPLETED,"valid output can deliberately retry the same paused context");
 CHECK(value,0x6677,"retry resumes the exact previous yield");CHECK(rt->error,DC_GOAL_OK,"successful retry clears call argument error");
 CHECK(dc_goal_fiber_current()==NULL,1,"retry returns to root");dc_goal_clear_process_context(rt);
 /* Pause afresh, corrupt one authorization field, and prove no restore/jump. */
 for(j=0;j<8;++j){
  uint32_t peak_before;
  CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"fresh context for one independent saved-state corruption");
  CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"corruption probe explicit owner binding");
  CHECK(dc_goal_fiber_step(&fibers[0],0,&value),DC_FIBER_YIELDED,"valid image exists before corruption");
  peak_before=fibers[0].peak_requested_image_bytes;
  memcpy(backup_before,dc_goal_resolve(rt,threads[0]+124,32),32);memset(dc_goal_resolve(rt,base,16384),0xd3,16384);
  symbol=dc_goal_find(rt,"dc-fiber-native-body");old_value=dc_goal_symbol_value(rt,symbol);
  if(j==0)fibers[0].native_thread.pr=(uint32_t)(uintptr_t)dc_goal_context_boot_raw;
  if(j==1)--fibers[0].native_thread.sp;
  if(j==2)fibers[0].native_thread.fpscr|=0x00080000;
  if(j==3)--fibers[0].image.image_sp;
  if(j==4)dc_goal_set_symbol(rt,symbol,rt->s7);
  if(j==5){uint32_t v=8193;memcpy(dc_goal_resolve(rt,threads[0]+32,4),&v,4);}
  if(j==6)fibers[0].image.saved=0;
  if(j==7)++fibers[0].image.image_bytes;
  value=0x1122334455667788ull;
  CHECK(dc_goal_fiber_step(&fibers[0],99,&value),DC_FIBER_FAIL,"malformed/stale paused authorization never resumes");
  CHECK(fibers[0].phase,DC_FIBER_FAULT,"saved-state failure invalidates context");
  CHECK(fibers[0].peak_requested_image_bytes,peak_before,"invalid saved authorization cannot change validated suspension observation");
  CHECK(value,0x1122334455667788ull,"failed continuation keeps caller output");
  CHECK(filled_v(dc_goal_resolve(rt,base,16384),16384,0xd3),1,"invalid continuation rejected before destination restoration");
  CHECK(memcmp(backup_before,dc_goal_resolve(rt,threads[0]+124,32),32),0,"invalid continuation leaves backup payload unchanged");
  if(j==4)dc_goal_set_symbol(rt,symbol,old_value);
  if(j==5){uint32_t v=8192;memcpy(dc_goal_resolve(rt,threads[0]+32,4),&v,4);}
  CHECK(dc_goal_fiber_step(&fibers[0],0,&value),0,"repair alone cannot resurrect faulted context");
  CHECK(fibers[0].phase,DC_FIBER_FAULT,"fault remains terminal after repair");
  dc_goal_clear_process_context(rt);
 }
 /* Active metadata mutation/reentry cannot abandon the fresh root anchor. */
 for(native_action=1;native_action<=6;++native_action){
  CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"fresh active-operation rejection context");
  CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"active-operation owner bound");
  native_rejected=99;value=0x1122334455667788ull;
  CHECK(guarded_step(&fibers[0],0,&value,&intact),0,"active clear/reinit/nested step fails through normal root return");
  CHECK(native_rejected,0,"active metadata operation rejected before mutation");
  CHECK(intact,1,"active rejection still restores root C state");
  CHECK(fibers[0].phase,DC_FIBER_FAULT,"callee error closes active context");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"active scope violation error explicit");
  CHECK(value,0x1122334455667788ull,"active scope failure preserves output");
  CHECK(dc_goal_fiber_current()==NULL,1,"normal fault return releases active-scope pointer");
  dc_goal_clear_process_context(rt);
 }
 native_action=0;
 /* Zero declared backup budget must fault after yield without a partial copy. */
 {uint32_t v=0;memcpy(dc_goal_resolve(rt,threads[0]+32,4),&v,4);}
 CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"zero backup budget may initialize without inventing capacity");
 CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"zero-budget owner bound");
 memcpy(backup_before,dc_goal_resolve(rt,threads[0]+124,32),32);value=0x1122334455667788ull;
 CHECK(guarded_step(&fibers[0],0,&value,&intact),0,"insufficient backup faults at first real suspension");
 CHECK(intact,1,"backup OOM restores root CPU state");CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"backup capacity error remains visible");
 CHECK(fibers[0].phase,DC_FIBER_FAULT,"uncopyable continuation cannot resume");
 CHECK(fibers[0].peak_requested_image_bytes>0 && fibers[0].peak_requested_image_bytes<=16384,1,"backup OOM retains validated suspension requirement");
 CHECK(value,0x1122334455667788ull,"backup failure preserves caller output");
 CHECK(memcmp(backup_before,dc_goal_resolve(rt,threads[0]+124,32),32),0,"zero budget performs no payload copy");
 {uint32_t v=8192;memcpy(dc_goal_resolve(rt,threads[0]+32,4),&v,4);}
 CHECK(dc_goal_fiber_step(&fibers[0],0,&value),0,"repairing budget does not resurrect failed continuation");
 dc_goal_clear_process_context(rt);
 for(i=0;i<2;++i)CHECK(memcmp(header[i],dc_goal_resolve(rt,threads[i]-4,128),128),0,"all cooperative paths preserve originalCPU register header");
 /* Clear abandons control metadata only, with no implicit unwind or payload erase. */
 CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"fresh context for explicit suspended disposal");
 CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"disposal probe owner bound");
 CHECK(dc_goal_fiber_step(&fibers[0],0,&value),DC_FIBER_YIELDED,"disposal has a real pending native image");
 memcpy(backup_before,dc_goal_resolve(rt,threads[0]+124,32),32);
 CHECK(dc_goal_fiber_clear(&fibers[0]),1,"explicit suspended disposal invalidates C control record");
 CHECK(fibers[0].phase,DC_FIBER_EMPTY,"disposal closes lifecycle");CHECK(fibers[0].image.valid|fibers[0].image.saved,0,"disposal releases image authorization");
 value=0x1122334455667788ull;CHECK(dc_goal_fiber_step(&fibers[0],0,&value),0,"disposed pending image cannot resume");
 CHECK(value,0x1122334455667788ull,"disposed context preserves output");
 CHECK(memcmp(backup_before,dc_goal_resolve(rt,threads[0]+124,32),32),0,"disposal intentionally does not erase original backup bytes");
 dc_goal_clear_process_context(rt);
 /* Removing the bound core invalidates a paused context before any restore. */
 CHECK(dc_goal_fiber_init(&fibers[0],rt,owners[0],12400,threads[0],base,16384,native_entry),1,"fresh context for core lifetime check");
 CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"core lifetime owner bound");
 CHECK(dc_goal_fiber_step(&fibers[0],0,&value),DC_FIBER_YIELDED,"core lifetime probe pauses normally");
 dc_goal_clear_core_bridges(rt);memset(dc_goal_resolve(rt,base,16384),0xd3,16384);value=0x1122334455667788ull;
 CHECK(dc_goal_fiber_step(&fibers[0],1,&value),0,"missing active core prevents context resume");
 CHECK(fibers[0].phase,DC_FIBER_FAULT,"core ownership failure closes context");
 CHECK(filled_v(dc_goal_resolve(rt,base,16384),16384,0xd3),1,"core ownership checked before restoring image");
 CHECK(value,0x1122334455667788ull,"missing core preserves caller output");
 CHECK(dc_goal_fiber_clear(&fibers[0]),1,"first terminal metadata released");CHECK(dc_goal_fiber_clear(&fibers[1]),1,"second terminal metadata released");
 CHECK(rt->used,used,"cooperative steps allocate no arena bytes");CHECK(rt->symbol_count,symbols,"cooperative steps intern no names");
 dc_goal_set_method(rt,rt->object_type,0,old_object);dc_goal_set_method(rt,proc_type,0,old_process);dc_goal_clear_process_heap(rt);rt->error=DC_GOAL_OK;
#undef CHECK
 return bridge.code_bytes+module.code_bytes+native_module.code_bytes;
}
