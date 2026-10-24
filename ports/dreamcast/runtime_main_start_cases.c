/* Original arming bytes -> explicit native startup (AI-assisted), ISC. */
#include "goal_main_start.h"
#include "goal_main_thread.h"
#include "goal_bridge.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include "build/original_set_to_run.h"
#include <stdlib.h>
#include <string.h>
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *s){return dc_goal_symbol_value(rt,dc_goal_find(rt,s));}
static uint32_t word(dc_goal_runtime *rt,uint32_t p,unsigned o){uint32_t v;memcpy(&v,dc_goal_resolve(rt,p+o,4),4);return v;}
static void patch(void *p,unsigned o,uint32_t v){memcpy((unsigned char *)p+o,&v,4);}
static int filled(const void *p,unsigned n,unsigned char v){const unsigned char *b=p;while(n--)if(*b++!=v)return 0;return 1;}
static unsigned alias_calls,active_preflight,active_result;
extern uint32_t dc_runtime_profile_images[2];
static uint64_t alias_probe(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++alias_calls;return 0;
}
static uint64_t active_probe(void){
 uint64_t args[6]={0};dc_goal_fiber *f=dc_goal_fiber_current();if(!f)return 0;
 active_preflight=(unsigned)dc_goal_main_thread_check(f->runtime,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,0,0);
 active_result=(unsigned)dc_goal_arm_main_thread(f->runtime,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,entry(f->runtime,"dc-fiber-words-six"),args);
 return 99;
}
uint32_t dc_runtime_main_start_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result guard={0},module={0},probes={0};dc_goal_fiber fiber;
 uint32_t proc=entry(rt,"process"),tree=entry(rt,"process-tree"),cpu=entry(rt,"cpu-thread"),owner,root,thread,name,allocation,stack;
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_proc=dc_goal_get_method(rt,proc,0),old_tree=dc_goal_get_method(rt,tree,0),old_cpu=dc_goal_get_method(rt,cpu,0);
 uint32_t target=entry(rt,"dc-fiber-words-six"),setter,bootstrap,set_symbol,boot_symbol,used,symbols,i,address;
 uint64_t args[6],wanted[6],sum=0,value,raw_target;const unsigned weights[6]={1,3,5,7,11,13};unsigned intact;
 unsigned char *before=malloc(12400),*expected=malloc(12400),header[128];
 unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
 dc_goal_code_export exports[2]={{"dc-main-start-active-probe",code[0],12,NULL,0},{"dc-main-start-alias-probe",code[1],12,NULL,0}};
 one new_tree=(one)(uintptr_t)entry(rt,"dc-new-tree");two new_proc=(two)(uintptr_t)entry(rt,"dc-new-process");
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(before && expected,1,"original arming byte oracle allocated");if(!before || !expected)goto done;
 CHECK(dc_goal_install_main_start_guard(rt,sync,&guard),1,"unsupported original assembly bootstrap gets fail-closed entry");
 CHECK(dc_goal_load_module(rt,goal_original_set_to_run_exports,GOAL_ORIGINAL_SET_TO_RUN_EXPORT_COUNT,sync,NULL,&module),1,"unchanged original set-to-run links against guard");
 if(!guard.functions || !module.functions)goto done;
 CHECK(module.functions,1,"original arming exports exactly one body");
 set_symbol=dc_goal_find(rt,"set-to-run");setter=entry(rt,"set-to-run");boot_symbol=dc_goal_find(rt,"set-to-run-bootstrap");bootstrap=entry(rt,"set-to-run-bootstrap");
 address=(uint32_t)(uintptr_t)active_probe;memcpy(code[0]+8,&address,4);address=(uint32_t)(uintptr_t)alias_probe;memcpy(code[1]+8,&address,4);
 CHECK(dc_goal_load_module(rt,exports,2,sync,NULL,&probes),1,"native arming rejection oracles registered");if(probes.functions!=2)goto done;
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"arming sized allocator selected");
 CHECK(dc_goal_set_method(rt,proc,0,entry(rt,"(method new process)")),1,"arming original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree,0,entry(rt,"(method new process-tree)")),1,"arming original tree constructor selected");
 CHECK(dc_goal_set_method(rt,cpu,0,entry(rt,"(method new cpu-thread)")),1,"arming original CPU constructor selected");
 dc_goal_clear_process_context(rt);allocation=dc_goal_alloc(rt,8224);stack=allocation+16;
 name=dc_goal_intern(rt,"dc-original-arming-owner");owner=(uint32_t)new_proc(name,12288);root=(uint32_t)new_tree(name);
 CHECK(allocation && owner && root,1,"original arming owns process/tree and fenced8KiB stack");if(!allocation || !owner || !root)goto restore;
 memset(dc_goal_resolve(rt,allocation,8224),0xa5,8224);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
 CHECK(dc_goal_activate_process(rt,owner,12400,root,stack,8192,name),owner,"unchanged activation creates ready main CPU for original arming");thread=word(rt,owner,40);
 if(!thread || thread==rt->s7)goto restore;
 CHECK(dc_goal_resize_main_backup(rt,owner,12400,thread,stack,8192,512),1,"original setter prepares measured six-argument backup before arming");
 used=rt->used;symbols=rt->symbol_count;memcpy(before,dc_goal_resolve(rt,owner-4,12400),12400);
 value=UINT64_C(0x1122334455667788);
 CHECK(dc_goal_run_initializer(rt,"set-to-run-bootstrap",&value),1,"unsupported bootstrap guard is callable without platform assembly");
 CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"unsupported original bootstrap reports failure even outside native fiber");CHECK(value,0,"unsupported bootstrap never supplies a game result");
 CHECK(memcmp(before,dc_goal_resolve(rt,owner-4,12400),12400),0,"bootstrap guard performs no owner effects");
 CHECK(dc_goal_fiber_init(&fiber,rt,owner,12400,thread,stack,8192,entry(rt,"dc-main-start-active-probe")),1,"live arming probe uses otherwise-ready original CPU");active_preflight=active_result=99;
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_FAIL,"active arming fails through normal native root return");
 CHECK(active_preflight,1,"same live arming geometry passes read-only preflight");CHECK(active_result,0,"active arming wrapper rejects before original stores");
 CHECK(intact,1,"active arming rejection preserves root CPU");CHECK(fiber.phase,DC_FIBER_FAULT,"active arming error closes native context");
 CHECK(memcmp(before,dc_goal_resolve(rt,owner-4,12400),12400),0,"active arming guard leaves entire owner unchanged");CHECK(dc_goal_fiber_clear(&fiber),1,"live probe metadata released before real arming");
 memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
 for(i=0;i<6;++i){args[i]=wanted[i]=UINT64_C(0xabcdef1234567890)+i*UINT64_C(0x1122334455667789);sum+=args[i]*weights[i];}
 for(i=0;i<5;++i){
  uint32_t requested=target;const uint64_t *input=args;alias_calls=0;
  if(i==0)dc_goal_set_symbol(rt,set_symbol,entry(rt,"dc-main-start-alias-probe"));
  if(i==1)dc_goal_set_symbol(rt,boot_symbol,entry(rt,"dc-main-start-alias-probe"));
  if(i==2)requested+=2;
  if(i==3)input=NULL;
  if(i==4)dc_goal_set_symbol(rt,set_symbol,rt->s7);
  rt->error=DC_GOAL_OK;
  CHECK(dc_goal_arm_main_thread(rt,owner,12400,thread,stack,8192,requested,input),0,"missing/foreign original exports or invalid startup input reject before arming");
  CHECK(rt->error,i==3?DC_GOAL_BAD_ARGUMENT:DC_GOAL_BAD_FUNCTION,"arming provenance versus input error explicit");CHECK(alias_calls,0,"foreign arming/bootstrap alias never called");
  CHECK(memcmp(before,dc_goal_resolve(rt,owner-4,12400),12400),0,"invalid arming preserves all owner bytes");
  dc_goal_set_symbol(rt,set_symbol,setter);dc_goal_set_symbol(rt,boot_symbol,bootstrap);
 }
 /* Input aliases the registers that the original body overwrites. */
 memcpy(dc_goal_resolve(rt,thread+36,48),args,48);memcpy(before,dc_goal_resolve(rt,owner-4,12400),12400);memcpy(expected,before,12400);
 patch(expected,owner+32-(owner-4),dc_goal_find(rt,"waiting-to-run"));patch(expected,thread+20-(owner-4),bootstrap);
 raw_target=target;memcpy(expected+thread+36-(owner-4),&raw_target,8);memcpy(expected+thread+44-(owner-4),args,48);
 CHECK(dc_goal_arm_main_thread(rt,owner,12400,thread,stack,8192,target,dc_goal_resolve(rt,thread+36,48)),1,"unchanged original arming copies six aliased64-bit input words before stores");
 CHECK(rt->error,DC_GOAL_OK,"original arming completes without callbacks");
 CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,12400),12400),0,"original body changes only status/PC/SP and seven original saved registers");
 CHECK(filled(dc_goal_resolve(rt,stack,8192),8192,0xd3),1,"original arming never runs code on working stack");
 CHECK(dc_goal_main_thread_check(rt,owner,12400,thread,stack,8192,1,bootstrap),1,"original serialized image passes armed state gate");
 CHECK(dc_goal_arm_main_thread(rt,owner,12400,thread,stack,8192,target,args),0,"already-armed thread cannot be rearmed through ready-only wrapper");
 CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,12400),12400),0,"rearm rejection leaves original saved words intact");
 memset(args,0,sizeof args);memcpy(before,expected,12400);
 for(i=0;i<6;++i){
  uint32_t v;memcpy(dc_goal_resolve(rt,owner-4,12400),before,12400);memset(&fiber,0x5a,sizeof fiber);
  if(i==0){v=dc_goal_find(rt,"ready");memcpy(dc_goal_resolve(rt,owner+32,4),&v,4);}
  if(i==1){v=bootstrap+2;memcpy(dc_goal_resolve(rt,thread+20,4),&v,4);}
  if(i==2){v=stack+8192-4;memcpy(dc_goal_resolve(rt,thread+24,4),&v,4);}
  if(i==3){raw_target=UINT64_C(0x100000000)|target;memcpy(dc_goal_resolve(rt,thread+36,8),&raw_target,8);}
  if(i==4){raw_target=target+2;memcpy(dc_goal_resolve(rt,thread+36,8),&raw_target,8);}
  if(i==5)dc_goal_set_symbol(rt,boot_symbol,entry(rt,"dc-main-start-alias-probe"));
  memcpy(expected,dc_goal_resolve(rt,owner-4,12400),12400);value=UINT64_C(0x1122334455667788);
  CHECK(dc_goal_fiber_init_main(&fiber,rt,owner,12400,thread,stack,8192),0,"malformed original startup image rejected before native entry");
  CHECK(rt->error,i<3?DC_GOAL_BAD_ARGUMENT:DC_GOAL_BAD_FUNCTION,"original image state versus entry error explicit");
  CHECK(fiber.phase,DC_FIBER_EMPTY,"failed original image import invalidates inactive owned metadata");
  CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,12400),12400),0,"rejected image import leaves owner unchanged");
  CHECK(filled(dc_goal_resolve(rt,stack,8192),8192,0xd3),1,"rejected image import never touches execution stack");
  dc_goal_set_symbol(rt,boot_symbol,bootstrap);
 }
 memcpy(dc_goal_resolve(rt,owner-4,12400),before,12400);memcpy(header,dc_goal_resolve(rt,thread-4,128),128);
 CHECK(dc_goal_fiber_init_main(&fiber,rt,owner,12400,thread,stack,8192),1,"native adapter imports actual original function and saved startup words");
 CHECK(fiber.entry,target,"native adapter target comes from originalrreg0");CHECK(fiber.argument_count,6,"original bootstrap protocol imports exactly six scalar arguments");
 CHECK(memcmp(fiber.arguments,wanted,sizeof wanted),0,"native adapter preserves both halves of all six saved words");
 CHECK(dc_goal_bind_process_context(rt,owner,12400),1,"originally armed body identity bound explicitly");
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_YIELDED,"original saved startup image executes native six-argument GOAL body");
 CHECK(intact,1,"original startup import preserves root CPU state");CHECK(value,sum^UINT64_C(0x123456789abcdef0),"original saved arguments supply exact first yield");
 CHECK(fiber.peak_requested_image_bytes,dc_runtime_profile_images[0],"original imported fixture matches measured six-argument suspension budget");
 dc_goal_clear_process_context(rt);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
 CHECK(dc_goal_bind_process_context(rt,owner,12400),1,"original startup continuation identity explicitly rebound");
 CHECK(dc_runtime_fiber_checked_step(&fiber,UINT64_C(0x9876543210abcdef),&value,&intact),DC_FIBER_COMPLETED,"imported original startup finishes after exact poisoned-stack restoration");
 CHECK(intact,1,"original startup completion preserves root CPU state");CHECK(value,sum+UINT64_C(0x9876543210abcdef),"original saved arguments survive native suspension");dc_goal_clear_process_context(rt);
 CHECK(memcmp(header,dc_goal_resolve(rt,thread-4,128),128),0,"native adapter does not masquerade as original register restore");
 CHECK(word(rt,owner,32),dc_goal_find(rt,"waiting-to-run"),"explicit diagnostic driver leaves original scheduler state unchanged");
 CHECK(dc_goal_fiber_clear(&fiber),1,"completed imported context released");
 CHECK(filled(dc_goal_resolve(rt,allocation,16),16,0xa5),1,"original armed startup lower stack fence");CHECK(filled(dc_goal_resolve(rt,stack+8192,16),16,0xa5),1,"original armed startup upper stack fence");
 CHECK(rt->used,used,"arming/import/yield paths allocate no arena bytes");CHECK(rt->symbol_count,symbols,"arming/import/yield paths intern no names");
restore:
 dc_goal_set_method(rt,rt->object_type,0,old_object);dc_goal_set_method(rt,proc,0,old_proc);dc_goal_set_method(rt,tree,0,old_tree);dc_goal_set_method(rt,cpu,0,old_cpu);dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
done: free(before);free(expected);
#undef CHECK
 return guard.code_bytes+module.code_bytes+probes.code_bytes;
}
