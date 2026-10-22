/* Actual read-only C validator on host and SH4 (AI-assisted), ISC. */
#include "thread_resize_cases.h"
#include "goal_thread_resize.h"
#include "goal_kernel_types.h"
#include <stdlib.h>
#include <string.h>
static uint32_t word(dc_goal_runtime *rt,uint32_t p){uint32_t v;memcpy(&v,dc_goal_resolve(rt,p,4),4);return v;}
static void put(dc_goal_runtime *rt,uint32_t p,uint32_t v){memcpy(dc_goal_resolve(rt,p,4),&v,4);}
void dc_thread_resize_cases(dc_runtime_check check,void *context,unsigned *id){
 const uint32_t capacity=512u*1024u;
 unsigned char *allocation=malloc(capacity+16),*memory,*before=malloc(2160),*stack_before=malloc(8192);
 dc_goal_runtime rt;dc_kernel_types_result installed;
 uint32_t proc,cpu,thread_type,thread,stack,base,cur,used,symbols,i;
 const uint32_t good[]={0,16,128,256,512,1024};
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(allocation && before && stack_before,1,"resize checker C memory allocated");
 if(!allocation || !before || !stack_before)goto done;
 memory=(unsigned char *)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000),1,"resize checker real runtime initialized");
 CHECK(dc_goal_install_kernel_types(&rt,&installed),1,"resize checker real kernel namespace");
 thread_type=dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_THREAD_TYPE);
 cpu=dc_goal_define_type(&rt,"cpu-thread",thread_type,128,0,12);
 proc=dc_goal_new_basic(&rt,dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_PROCESS_TYPE),2160);
 stack=dc_goal_alloc(&rt,8192);CHECK(cpu && proc && stack,1,"resize checker owned typed objects and stack");
 if(!cpu || !proc || !stack)goto done;
 base=proc+108;thread=base+4;cur=base+384;
 put(&rt,proc+68,2048);put(&rt,proc+76,base);put(&rt,proc+80,base+2048);put(&rt,proc+84,cur);
 put(&rt,proc+40,thread);put(&rt,proc+44,thread);put(&rt,thread-4,cpu);put(&rt,thread+4,proc);
 put(&rt,thread+8,rt.s7);put(&rt,thread+20,0);put(&rt,thread+24,stack+8192);put(&rt,thread+28,stack+8192);put(&rt,thread+32,256);
 memset(dc_goal_resolve(&rt,thread+124,256),0xa5,256);memset(dc_goal_resolve(&rt,stack,8192),0xd3,8192);
 used=rt.used;symbols=rt.symbol_count;memcpy(before,dc_goal_resolve(&rt,proc-4,2160),2160);memcpy(stack_before,dc_goal_resolve(&rt,stack,8192),8192);
 for(i=0;i<sizeof good/sizeof good[0];++i){rt.error=DC_GOAL_OK;
  CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,stack,8192,good[i]),1,"valid resize/no-op preflight without execution");
  CHECK(rt.error,DC_GOAL_OK,"valid resize preflight resets error");
  CHECK(memcmp(before,dc_goal_resolve(&rt,proc-4,2160),2160),0,"successful preflight leaves every process/thread/payload byte unchanged");
  CHECK(memcmp(stack_before,dc_goal_resolve(&rt,stack,8192),8192),0,"successful preflight leaves execution stack unchanged");
 }
 {
  struct bad_size{uint32_t size;enum dc_goal_error error;} bad[]={
   {1,DC_GOAL_BAD_ARGUMENT},{8208,DC_GOAL_BAD_ARGUMENT},{UINT32_MAX,DC_GOAL_BAD_ARGUMENT},
   {0x80000000u,DC_GOAL_BAD_ARGUMENT},{1920,DC_GOAL_OUT_OF_MEMORY},{1936,DC_GOAL_OUT_OF_MEMORY}};
  for(i=0;i<sizeof bad/sizeof bad[0];++i){rt.error=DC_GOAL_OK;
   CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,stack,8192,bad[i].size),0,"unsupported resize request rejected");
   CHECK(rt.error,bad[i].error,"resize rejection explicit error class");
   CHECK(memcmp(before,dc_goal_resolve(&rt,proc-4,2160),2160),0,"size rejection leaves complete owner unchanged");
   CHECK(memcmp(stack_before,dc_goal_resolve(&rt,stack,8192),8192),0,"size rejection leaves stack unchanged");
  }
 }
 {
  struct bad_field{uint32_t at,value;enum dc_goal_error error;} bad[]={
   {proc+40,rt.s7,DC_GOAL_BAD_ARGUMENT},{thread+8,0,DC_GOAL_BAD_ARGUMENT},{thread+20,1,DC_GOAL_BAD_ARGUMENT},
   {thread+24,stack+8188,DC_GOAL_BAD_ARGUMENT},{proc+84,cur+16,DC_GOAL_BAD_ARGUMENT},
   {thread+32,240,DC_GOAL_BAD_ARGUMENT},{thread+28,stack+8208,DC_GOAL_BAD_ARGUMENT},
   {proc+44,rt.s7,DC_GOAL_BAD_ARGUMENT},{proc+80,base+2064,DC_GOAL_BAD_ARGUMENT},
   {proc+84,base-16,DC_GOAL_BAD_ARGUMENT},{thread-4,thread_type,DC_GOAL_BAD_TYPE},
   {proc-4,rt.object_type,DC_GOAL_BAD_TYPE}};
  for(i=0;i<sizeof bad/sizeof bad[0];++i){uint32_t saved=word(&rt,bad[i].at);put(&rt,bad[i].at,bad[i].value);
   memcpy(before,dc_goal_resolve(&rt,proc-4,2160),2160);rt.error=DC_GOAL_OK;
   CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,stack,8192,512),0,"stale/unsupported main-thread state rejected");
   CHECK(rt.error,bad[i].error,"stale resize state explicit error class");
   CHECK(memcmp(before,dc_goal_resolve(&rt,proc-4,2160),2160),0,"state rejection never repairs or edits caller object");
   CHECK(memcmp(stack_before,dc_goal_resolve(&rt,stack,8192),8192),0,"state rejection leaves working stack");
   put(&rt,bad[i].at,saved);
  }
 }
 /* New extent cannot grow through a working stack in the owner's free heap:
  * existing image-open excludes the ENTIRE process reservation, not just the
  * old header/backup. Keep other header geometry consistent to isolate it. */
 put(&rt,thread+24,base+512+4096);put(&rt,thread+28,base+512+4096);
 memcpy(before,dc_goal_resolve(&rt,proc-4,2160),2160);rt.error=DC_GOAL_OK;
 CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,base+512,4096,512),0,"working stack inside free process heap rejected before growth");
 CHECK(rt.error,DC_GOAL_BAD_ARGUMENT,"full-reservation stack overlap explicit error");
 CHECK(memcmp(before,dc_goal_resolve(&rt,proc-4,2160),2160),0,"free-heap overlap rejects without editing owner");
 CHECK(memcmp(stack_before,dc_goal_resolve(&rt,stack,8192),8192),0,"free-heap overlap leaves actual working stack untouched");
 put(&rt,thread+24,stack+8192);put(&rt,thread+28,stack+8192);
 rt.error=DC_GOAL_OK;CHECK(dc_goal_thread_resize_check(&rt,0,2160,thread,stack,8192,512),0,"underflow process rejected by existing heap gate");CHECK(rt.error,DC_GOAL_BAD_ARGUMENT,"underflow process error explicit");
 rt.error=DC_GOAL_OK;CHECK(dc_goal_thread_resize_check(&rt,proc,2160,0,stack,8192,512),0,"underflow thread rejected even before no-op/resize split");CHECK(rt.error,DC_GOAL_BAD_ARGUMENT,"underflow thread error explicit");
 rt.error=DC_GOAL_OK;CHECK(dc_goal_thread_resize_check(&rt,UINT32_MAX-16,2160,thread,stack,8192,512),0,"wrapped reservation rejected before overlap arithmetic");CHECK(rt.error,DC_GOAL_BAD_ARGUMENT,"wrapped reservation error explicit");
 put(&rt,proc+84,cur+16);memcpy(before,dc_goal_resolve(&rt,proc-4,2160),2160);
 CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,stack,8192,256),1,"same-size original no-op allowed after later allocation");
 CHECK(memcmp(before,dc_goal_resolve(&rt,proc-4,2160),2160),0,"same-size no-op preflight preserves later allocation");put(&rt,proc+84,cur);
 CHECK(dc_goal_thread_resize_check(&rt,proc,2161,thread,stack,8192,512),0,"wrong reservation rejected");
 CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,stack+1,8192,512),0,"misaligned execution base rejected");
 CHECK(dc_goal_thread_resize_check(&rt,proc,2160,thread,stack,8191,512),0,"misaligned execution size rejected");
 CHECK(dc_goal_thread_resize_check(NULL,proc,2160,thread,stack,8192,512),0,"null runtime rejected");
 CHECK(rt.used,used,"resize preflight never allocates");CHECK(rt.symbol_count,symbols,"resize preflight never interns");
done:
 free(allocation);free(before);free(stack_before);
#undef CHECK
}
