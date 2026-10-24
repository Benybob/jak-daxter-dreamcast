/* Independent read-only state/ownership oracle (AI-assisted), ISC. */
#include "main_thread_cases.h"
#include "goal_main_thread.h"
#include "goal_kernel_types.h"
#include <stdlib.h>
#include <string.h>
static void put(dc_goal_runtime *rt,uint32_t p,uint32_t v){memcpy(dc_goal_resolve(rt,p,4),&v,4);}
void dc_main_thread_cases(dc_runtime_check check,void *context,unsigned *id){
 const uint32_t capacity=512u*1024u;
 unsigned char *allocation=malloc(capacity+16),*before=malloc(capacity),*memory;
 dc_goal_runtime rt;dc_kernel_types_result installed;
 uint32_t proc,cpu,thread,base,stack,ready,waiting,used,symbols,i,state;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(allocation && before,1,"main-thread checker memory available");if(!allocation || !before)goto done;
 memory=(unsigned char *)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000),1,"main-thread checker real arena initialized");
 CHECK(dc_goal_install_kernel_types(&rt,&installed),1,"main-thread checker original namespace installed");
 cpu=dc_goal_define_type(&rt,"cpu-thread",dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_THREAD_TYPE),128,0,12);
 proc=dc_goal_new_basic(&rt,dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_PROCESS_TYPE),2160);stack=dc_goal_alloc(&rt,8192);
 ready=dc_goal_intern(&rt,"ready");waiting=dc_goal_intern(&rt,"waiting-to-run");
 CHECK(cpu && proc && stack && ready && waiting,1,"main-thread checker owns typed processCPU and aligned stack");
 if(!cpu || !proc || !stack || !ready || !waiting)goto done;
 base=proc+108;thread=base+4;
 put(&rt,proc+68,2048);put(&rt,proc+76,base);put(&rt,proc+80,base+2048);put(&rt,proc+84,base+400);
 put(&rt,proc+40,thread);put(&rt,proc+44,thread);put(&rt,thread-4,cpu);put(&rt,thread+4,proc);
 put(&rt,thread+8,rt.s7);put(&rt,thread+24,stack+8192);put(&rt,thread+28,stack+8192);put(&rt,thread+32,256);
 /* A real later allocation remains in the owner; arming does not resize it. */
 used=rt.used;symbols=rt.symbol_count;
 for(state=0;state<2;++state){
  put(&rt,proc+32,state?waiting:ready);put(&rt,thread+20,state?0x12345678:0);
  memcpy(before,memory,used);rt.error=DC_GOAL_BAD_ARGUMENT;
  CHECK(dc_goal_main_thread_check(&rt,proc,2160,thread,stack,8192,state,0x12345678),1,"ready/armed policy accepts valid owner with later allocation");
  CHECK(rt.error,DC_GOAL_OK,"valid main state clears stale error");CHECK(memcmp(before,memory,used),0,"valid main checker is read-only over entire arena");
  for(i=0;i<14;++i){
   uint32_t p=proc,t=thread,b=stack,n=8192,r=2160,boot=0x12345678;unsigned armed=state;
   memcpy(memory,before,used);
   if(i==0)put(&rt,proc+40,rt.s7);
   if(i==1)put(&rt,thread+8,0);
   if(i==2)put(&rt,thread+24,stack+8192-4);
   if(i==3)put(&rt,proc+32,state?ready:waiting);
   if(i==4)put(&rt,thread+20,state?0:0x12345678);
   if(i==5)put(&rt,thread+32,255);
   if(i==6){b=stack+1;n=8191;}
   if(i==7){n=4080;put(&rt,thread+24,b+n);put(&rt,thread+28,b+n);}
   if(i==8)armed=2;
   if(i==9)p=0;
   if(i==10)t=0;
   if(i==11)r=2161;
   if(i==12){b=base+512;n=4096;put(&rt,thread+24,b+n);put(&rt,thread+28,b+n);}
   if(i==13){if(state)boot|=1;else put(&rt,proc+44,rt.s7);}
   /* Preserve the deliberate invalid input itself as the read-only baseline. */
   {unsigned char header[2160];memcpy(header,dc_goal_resolve(&rt,proc-4,2160),2160);
    rt.error=DC_GOAL_OK;
    CHECK(dc_goal_main_thread_check(&rt,p,r,t,b,n,armed,boot),0,"invalid main-thread authorization rejected");
    CHECK(rt.error!=DC_GOAL_OK,1,"main-thread policy or helper error explicit");
    CHECK(memcmp(header,dc_goal_resolve(&rt,proc-4,2160),2160),0,"main-thread rejection leaves owner unchanged");
   }
  }
  memcpy(memory,before,used);
 }
 CHECK(dc_goal_main_thread_check(NULL,proc,2160,thread,stack,8192,1,0x12345678),0,"null main runtime rejected");
 CHECK(dc_goal_main_thread_check(&rt,proc,2160,thread,stack,8192,1,0),0,"armed zero bootstrap rejected");
 CHECK(rt.used,used,"main preflight never allocates");CHECK(rt.symbol_count,symbols,"main preflight never interns");
done: free(allocation);free(before);
#undef CHECK
}
