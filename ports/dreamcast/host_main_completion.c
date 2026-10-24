/* Independent host completion receipt oracle (AI-assisted), ISC.
 * No SH4 execution: only this host fixture marks imported C metadata DONE.
 * Platform bridge liveness is modeled; all layout/import/completion code is real. */
#include "goal_main_start.h"
#include "goal_main_thread.h"
#include "goal_bridge.h"
#include "goal_kernel_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const dc_goal_runtime *active;
int dc_goal_core_bridges_active(const dc_goal_runtime *rt){return active==rt;}
static unsigned total,passed;
#define CHECK(x) do{++total;if(x)++passed;else fprintf(stderr,"FAIL%u line%u\n",total,(unsigned)__LINE__);}while(0)
static void put(dc_goal_runtime *rt,uint32_t p,uint32_t v){memcpy(dc_goal_resolve(rt,p,4),&v,4);}
static int zero(const void *p,size_t n){const unsigned char *b=p;while(n--)if(*b++)return 0;return 1;}
int main(void){
 const uint32_t capacity=512u*1024u;
 unsigned char *allocation=malloc(capacity+16),*arena=malloc(capacity),*memory;
 dc_goal_runtime rt,baseline,foreign;dc_kernel_types_result installed;dc_goal_module_result module;
 dc_goal_fiber f,good,copy,saved;uint32_t cpu,proc,thread,base,stack,bootstrap,target,waiting,ready,boot_symbol,target_symbol,i,used;
 uint64_t words[7]={0,UINT64_C(0x1122334455667788),UINT64_C(0xaabbccdd01234567),9,UINT64_C(0x8000000000000001),0,UINT64_C(0xfedcba9876543210)};
 const unsigned char code[4]={0x0b,0,9,0};dc_goal_code_export exports[2]={{"set-to-run-bootstrap",code,4,NULL,0},{"dc-completion-target",code,4,NULL,0}};
 CHECK(allocation && arena);if(!allocation || !arena)goto done;
 memory=(unsigned char *)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
 CHECK(dc_goal_init(&rt,memory,capacity,0x04000000));CHECK(dc_goal_install_kernel_types(&rt,&installed));active=&rt;
 cpu=dc_goal_define_type(&rt,"cpu-thread",dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_THREAD_TYPE),128,0,12);
 proc=dc_goal_new_basic(&rt,dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_PROCESS_TYPE),2160);stack=dc_goal_alloc(&rt,8192);
 CHECK(cpu && proc && stack);if(!cpu || !proc || !stack)goto done;
 CHECK(dc_goal_load_module(&rt,exports,2,NULL,NULL,&module));
 boot_symbol=dc_goal_find(&rt,"set-to-run-bootstrap");target_symbol=dc_goal_find(&rt,"dc-completion-target");
 bootstrap=dc_goal_symbol_value(&rt,boot_symbol);target=dc_goal_symbol_value(&rt,target_symbol);words[0]=target;
 waiting=dc_goal_intern(&rt,"waiting-to-run");ready=dc_goal_intern(&rt,"ready");
 base=proc+108;thread=base+4;
 put(&rt,proc+68,2048);put(&rt,proc+76,base);put(&rt,proc+80,base+2048);put(&rt,proc+84,base+400);
 put(&rt,proc+32,waiting);put(&rt,proc+36,0x12345678);put(&rt,proc+40,thread);put(&rt,proc+44,thread);
 put(&rt,thread-4,cpu);put(&rt,thread+4,proc);put(&rt,thread+8,rt.s7);put(&rt,thread+20,bootstrap);
 put(&rt,thread+24,stack+8192);put(&rt,thread+28,stack+8192);put(&rt,thread+32,256);
 memcpy(dc_goal_resolve(&rt,thread+36,56),words,56);
 CHECK(dc_goal_fiber_init_main(&f,&rt,proc,2160,thread,stack,8192));
 CHECK(f.main_import_self==(uintptr_t)&f);CHECK(f.main_import_pid==0x12345678);
 CHECK(f.phase==DC_FIBER_NEW);saved=f;rt.error=DC_GOAL_OK;
 CHECK(!dc_goal_main_completion_check(&f,&rt));CHECK(rt.error==DC_GOAL_BAD_ARGUMENT);CHECK(!memcmp(&saved,&f,sizeof f));
 /* Unit fixture only, never a claim that host executed the target. */
 f.phase=DC_FIBER_DONE;f.value=UINT64_C(0xfedcba9812345678);rt.error=DC_GOAL_OK;good=f;baseline=rt;used=rt.used;memcpy(arena,memory,used);
 CHECK(dc_goal_main_completion_check(&f,&rt));CHECK(!memcmp(arena,memory,used));CHECK(!memcmp(&good,&f,sizeof f));
 CHECK(dc_goal_main_completion_check(&f,&rt));
 for(i=0;i<33;++i){
  f=good;rt=baseline;memcpy(memory,arena,used);foreign=rt;
  if(i==0)f.phase=DC_FIBER_NEW;
  if(i==1)f.phase=DC_FIBER_SUSPENDED;
  if(i==2)f.phase=DC_FIBER_FAULT;
  if(i==3)f.magic=0;
  if(i==4)f.main_import_self=0;
  if(i==5)++f.main_import_self;
  if(i==6)f.main_import_pid^=1;
  if(i==7)put(&rt,proc+36,0x12345679);
  if(i==8)f.arguments[0]^=UINT64_C(0x100000000);
  if(i==9)f.arguments[6]=1;
  if(i==10)f.entry+=2;
  if(i==11)f.runtime=&foreign;
  if(i==12)f.image.valid=0;
  if(i==13)f.image.saved=1;
  if(i==14)f.image.heap.process+=16;
  if(i==15)f.argument_count=0;
  if(i==16)f.root_live=1;
  if(i==17)put(&rt,proc+32,ready);
  if(i==18)put(&rt,thread+20,bootstrap+2);
  if(i==19)put(&rt,thread+24,stack+8192-4);
  if(i==20)put(&rt,thread+8,0);
  if(i==21)put(&rt,proc+40,rt.s7);
  if(i==22)put(&rt,proc+44,rt.s7);
  if(i==23)put(&rt,thread+32,240);
  if(i==24){uint64_t bad_target=UINT64_C(0x100000000)|target;memcpy(dc_goal_resolve(&rt,thread+36,8),&bad_target,8);}
  if(i==25){uint64_t changed=words[6]^1;memcpy(dc_goal_resolve(&rt,thread+84,8),&changed,8);}
  if(i==26)dc_goal_set_symbol(&rt,boot_symbol,target);
  if(i==27)dc_goal_set_symbol(&rt,target_symbol,bootstrap);
  if(i==28)f.stack_bytes-=16;
  if(i==29)--f.reservation;
  if(i==30)f.main_import_pid=0;
  if(i==31)rt.code_registry_count=0;
  if(i==32){f.main_import_pid=0;put(&rt,proc+36,0);}
  {unsigned char *snapshot=malloc(used);CHECK(snapshot!=NULL);if(!snapshot)goto done;
   saved=f;memcpy(snapshot,memory,used);rt.error=DC_GOAL_OK;
   CHECK(!dc_goal_main_completion_check(&f,&rt));CHECK(rt.error!=DC_GOAL_OK);
   CHECK(!memcmp(&saved,&f,sizeof f));CHECK(!memcmp(snapshot,memory,used));
   rt.error=DC_GOAL_OK;CHECK(!dc_goal_main_completion_release(&f,&rt));
   CHECK(!memcmp(&saved,&f,sizeof f));CHECK(!memcmp(snapshot,memory,used));free(snapshot);
  }
 }
 rt=baseline;memcpy(memory,arena,used);f=good;copy=good;saved=copy;
 CHECK(!dc_goal_main_completion_check(&copy,&rt));CHECK(!memcmp(&saved,&copy,sizeof copy));
 f=good;rt=baseline;rt.error=DC_GOAL_OUT_OF_MEMORY;
 CHECK(!dc_goal_main_completion_check(&f,&rt));CHECK(rt.error==DC_GOAL_OUT_OF_MEMORY);CHECK(!memcmp(&good,&f,sizeof f));
 CHECK(!dc_goal_main_completion_release(&f,&rt));CHECK(rt.error==DC_GOAL_OUT_OF_MEMORY);
 CHECK(!dc_goal_main_completion_check(NULL,&rt));CHECK(rt.error==DC_GOAL_OUT_OF_MEMORY);
 rt=baseline;CHECK(!dc_goal_main_completion_check(NULL,&rt));CHECK(rt.error==DC_GOAL_BAD_ARGUMENT);
 CHECK(!dc_goal_main_completion_check(&f,NULL));CHECK(!memcmp(&good,&f,sizeof f));
 rt=baseline;active=NULL;CHECK(!dc_goal_main_completion_check(&f,&rt));CHECK(!memcmp(&good,&f,sizeof f));active=&rt;
 rt=baseline;CHECK(dc_goal_fiber_init_words(&f,&rt,proc,2160,thread,stack,8192,target,6,words+1));
 CHECK(f.main_import_self==0 && f.main_import_pid==0);f.phase=DC_FIBER_DONE;
 CHECK(!dc_goal_main_completion_check(&f,&rt));CHECK(f.phase==DC_FIBER_DONE);
 rt=baseline;put(&rt,proc+36,0);CHECK(!dc_goal_fiber_init_main(&f,&rt,proc,2160,thread,stack,8192));
 CHECK(f.phase==DC_FIBER_EMPTY && !f.main_import_self && !f.main_import_pid);CHECK(rt.error==DC_GOAL_BAD_ARGUMENT);
 {uint32_t pid;memcpy(&pid,dc_goal_resolve(&rt,proc+36,4),4);CHECK(pid==0);}
 memcpy(memory,arena,used);rt=baseline;
 {dc_goal_fiber *alias=(dc_goal_fiber *)(void *)(memory+((used+31u)&~15u));*alias=good;alias->main_import_self=(uintptr_t)alias;saved=*alias;
  CHECK(!dc_goal_main_completion_check(alias,&rt));CHECK(!memcmp(alias,&saved,sizeof saved));}
 rt=baseline;CHECK(!dc_goal_main_completion_check((dc_goal_fiber *)(void *)&rt,&rt));CHECK(rt.memory==memory && rt.used==used);
 f=good;rt=baseline;CHECK(dc_goal_main_completion_release(&f,&rt));CHECK(zero(&f,sizeof f));CHECK(!memcmp(arena,memory,used));
 CHECK(!dc_goal_main_completion_release(&f,&rt));CHECK(zero(&f,sizeof f));
 rt.error=DC_GOAL_OK;CHECK(dc_goal_main_thread_check(&rt,proc,2160,thread,stack,8192,1,bootstrap));
 CHECK(rt.used==baseline.used && rt.symbol_count==baseline.symbol_count && rt.code_registry_count==baseline.code_registry_count);
 /* Re-import is still mechanically possible: production driver forbids replay. */
 CHECK(dc_goal_fiber_init_main(&f,&rt,proc,2160,thread,stack,8192));CHECK(f.main_import_self==(uintptr_t)&f);
 CHECK(dc_goal_fiber_clear(&f));CHECK(zero(&f,sizeof f));
done:free(allocation);free(arena);printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);return total!=passed;
}
