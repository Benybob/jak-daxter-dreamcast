/* Original arming bytes -> explicit native startup (AI-assisted), ISC. */
#include "goal_main_start.h"
#include "goal_main_thread.h"
#include "goal_bridge.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include "build/original_set_to_run.h"
#include <stdlib.h>
#include <kos/thread.h>
#include <string.h>
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *s){return dc_goal_symbol_value(rt,dc_goal_find(rt,s));}
static uint32_t word(dc_goal_runtime *rt,uint32_t p,unsigned o){uint32_t v;memcpy(&v,dc_goal_resolve(rt,p+o,4),4);return v;}
static void patch(void *p,unsigned o,uint32_t v){memcpy((unsigned char *)p+o,&v,4);}
static int filled(const void *p,unsigned n,unsigned char v){const unsigned char *b=p;while(n--)if(*b++!=v)return 0;return 1;}
static unsigned alias_calls,active_preflight,active_result,active_completion,active_phase;
extern uint32_t dc_runtime_profile_images[2];
typedef struct {dc_goal_fiber *f;dc_goal_runtime *rt;int result;} completion_foreign;
static void *foreign_completion(void *data){completion_foreign *c=data;c->result=dc_goal_main_completion_release(c->f,c->rt);return NULL;}
static uint64_t alias_probe(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;++alias_calls;return 0;
}
static uint64_t active_probe(void){
 uint64_t args[6]={0};dc_goal_fiber *f=dc_goal_fiber_current();if(!f)return 0;
 active_preflight=(unsigned)dc_goal_main_thread_check(f->runtime,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,0,0);
 active_completion=(unsigned)dc_goal_main_completion_release(f,f->runtime);active_phase=f->phase;
 active_result=(unsigned)dc_goal_arm_main_thread(f->runtime,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,entry(f->runtime,"dc-fiber-words-six"),args);
 return 99;
}
uint32_t dc_runtime_main_start_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result guard={0},module={0},probes={0};dc_goal_fiber fiber,good,copy,saved;
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
 CHECK(active_completion,0,"completion release rejects an actually running native fiber");CHECK(active_phase,DC_FIBER_RUNNING,"active completion refusal leaves running anchor intact");
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
 CHECK(fiber.main_import_self,(uintptr_t)&fiber,"main import records exact caller C object address");CHECK(fiber.main_import_pid,word(rt,owner,36),"main import captures original activated PID");
 saved=fiber;rt->error=DC_GOAL_OK;CHECK(dc_goal_main_completion_check(&fiber,rt),0,"fresh imported image is not a completed body");CHECK(memcmp(&saved,&fiber,sizeof fiber),0,"fresh completion refusal preserves receipt");rt->error=DC_GOAL_OK;
 CHECK(memcmp(fiber.arguments,wanted,sizeof wanted),0,"native adapter preserves both halves of all six saved words");
 CHECK(dc_goal_bind_process_context(rt,owner,12400),1,"originally armed body identity bound explicitly");
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_YIELDED,"original saved startup image executes native six-argument GOAL body");
 CHECK(intact,1,"original startup import preserves root CPU state");CHECK(value,sum^UINT64_C(0x123456789abcdef0),"original saved arguments supply exact first yield");
 CHECK(fiber.peak_requested_image_bytes,dc_runtime_profile_images[0],"original imported fixture matches measured six-argument suspension budget");
 dc_goal_clear_process_context(rt);saved=fiber;rt->error=DC_GOAL_OK;
 CHECK(dc_goal_main_completion_release(&fiber,rt),0,"paused original-main image is not releasable completion");CHECK(memcmp(&saved,&fiber,sizeof fiber),0,"paused completion refusal preserves exact continuation");rt->error=DC_GOAL_OK;
 memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
 CHECK(dc_goal_bind_process_context(rt,owner,12400),1,"original startup continuation identity explicitly rebound");
 CHECK(dc_runtime_fiber_checked_step(&fiber,UINT64_C(0x9876543210abcdef),&value,&intact),DC_FIBER_COMPLETED,"imported original startup finishes after exact poisoned-stack restoration");
 CHECK(intact,1,"original startup completion preserves root CPU state");CHECK(value,sum+UINT64_C(0x9876543210abcdef),"original saved arguments survive native suspension");dc_goal_clear_process_context(rt);
 CHECK(memcmp(header,dc_goal_resolve(rt,thread-4,128),128),0,"native adapter does not masquerade as original register restore");
 CHECK(word(rt,owner,32),dc_goal_find(rt,"waiting-to-run"),"explicit diagnostic driver leaves original scheduler state unchanged");

 rt->error=DC_GOAL_OK;good=fiber;memcpy(before,dc_goal_resolve(rt,owner-4,12400),12400);
 CHECK(dc_goal_main_completion_check(&fiber,rt),1,"real imported native body yields a matching completion receipt");
 CHECK(dc_goal_main_completion_check(&fiber,rt),1,"quiescent completion check is repeatable");
 CHECK(memcmp(&good,&fiber,sizeof fiber),0,"completed result and full C witness remain unchanged");
 CHECK(memcmp(before,dc_goal_resolve(rt,owner-4,12400),12400),0,"completion check leaves complete process reservation unchanged");
 copy=good;rt->error=DC_GOAL_OK;
 CHECK(dc_goal_main_completion_check(&copy,rt),0,"copied completed object cannot claim original import identity");CHECK(memcmp(&copy,&good,sizeof copy),0,"copied-witness refusal is read-only");
 for(i=0;i<19;++i){
  uint32_t v;fiber=good;memcpy(dc_goal_resolve(rt,owner-4,12400),before,12400);
  if(i==0){fiber.main_import_self=0;fiber.main_import_pid=0;}
  if(i==1)fiber.main_import_self++;
  if(i==2)fiber.main_import_pid^=1;
  if(i==3){v=good.main_import_pid+1;memcpy(dc_goal_resolve(rt,owner+36,4),&v,4);}
  if(i==4)fiber.arguments[0]^=UINT64_C(0x100000000);
  if(i==5)fiber.arguments[7]=1;
  if(i==6){raw_target=(uint64_t)target|UINT64_C(0x100000000);memcpy(dc_goal_resolve(rt,thread+36,8),&raw_target,8);}
  if(i==7){raw_target=wanted[5]^1;memcpy(dc_goal_resolve(rt,thread+84,8),&raw_target,8);}
  if(i==8){v=dc_goal_find(rt,"ready");memcpy(dc_goal_resolve(rt,owner+32,4),&v,4);}
  if(i==9){v=bootstrap+2;memcpy(dc_goal_resolve(rt,thread+20,4),&v,4);}
  if(i==10){v=stack+8192-4;memcpy(dc_goal_resolve(rt,thread+24,4),&v,4);}
  if(i==11){v=496;memcpy(dc_goal_resolve(rt,thread+32,4),&v,4);}
  if(i==12)fiber.image.saved=1;
  if(i==13)fiber.image.valid=0;
  if(i==14)fiber.root_live=1;
  if(i==15)fiber.argument_count=7;
  if(i==16)dc_goal_set_symbol(rt,boot_symbol,entry(rt,"dc-main-start-alias-probe"));
  if(i==17)fiber.runtime=NULL;
  if(i==18){fiber.main_import_pid=0;v=0;memcpy(dc_goal_resolve(rt,owner+36,4),&v,4);}
  saved=fiber;memcpy(expected,dc_goal_resolve(rt,owner-4,12400),12400);rt->error=DC_GOAL_OK;
  CHECK(dc_goal_main_completion_check(&fiber,rt),0,"stale/foreign completion receipt or original header rejected");
  CHECK(rt->error!=DC_GOAL_OK,1,"completion failure reports explicit error");
  CHECK(memcmp(&saved,&fiber,sizeof fiber),0,"failed completion check preserves complete witness");
  CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,12400),12400),0,"failed completion check cannot repair original header");
  rt->error=DC_GOAL_OK;CHECK(dc_goal_main_completion_release(&fiber,rt),0,"failed completion cannot release diagnostic witness");
  CHECK(memcmp(&saved,&fiber,sizeof fiber),0,"failed completion release preserves result and imported words");
  dc_goal_set_symbol(rt,boot_symbol,bootstrap);
 }
 fiber=good;memcpy(dc_goal_resolve(rt,owner-4,12400),before,12400);rt->error=DC_GOAL_OK;
 {completion_foreign attempt={&fiber,rt,99};kthread_t *worker=thd_create(false,foreign_completion,&attempt);
  CHECK(worker!=NULL,1,"completion refusal oracle starts a real foreign OS thread");
  if(worker)CHECK(thd_join(worker,NULL),0,"foreign completion attempt joined before reading result");
  CHECK(attempt.result,0,"foreign OS owner cannot release a completed original-main witness");
  CHECK(memcmp(&good,&fiber,sizeof fiber),0,"foreign completion refusal retains complete native result");
  CHECK(memcmp(before,dc_goal_resolve(rt,owner-4,12400),12400),0,"foreign completion refusal leaves original owner unchanged");
  rt->error=DC_GOAL_OK;CHECK(dc_goal_main_completion_check(&fiber,rt),1,"original OS owner can still validate the refused witness");
 }
 fiber=good;memcpy(dc_goal_resolve(rt,owner-4,12400),before,12400);rt->error=DC_GOAL_OUT_OF_MEMORY;
 CHECK(dc_goal_main_completion_release(&fiber,rt),0,"existing callback/runtime failure prevents completion release");
 CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"completion release preserves preceding runtime failure");CHECK(memcmp(&good,&fiber,sizeof fiber),0,"prior error retains the completed witness");
 rt->error=DC_GOAL_OK;
 CHECK(dc_goal_main_completion_release(&fiber,rt),1,"validated native completion releases only C metadata");
 CHECK(filled(&fiber,sizeof fiber,0),1,"successful release clears the entire C receipt");
 CHECK(memcmp(before,dc_goal_resolve(rt,owner-4,12400),12400),0,"release does not deactivate/disarm original process");
 CHECK(dc_goal_main_completion_release(&fiber,rt),0,"second completion release cannot invent a receipt");
 rt->error=DC_GOAL_OK;CHECK(dc_goal_main_thread_check(rt,owner,12400,thread,stack,8192,1,bootstrap),1,"original header remains armed: driver replay prohibition remains explicit");
 CHECK(rt->used,used,"completion gate allocates no GOAL arena bytes");CHECK(rt->symbol_count,symbols,"completion gate interns no names");
 /* A new original activation supplies a genuinely separate PID generation. */
 {uint32_t next_owner=(uint32_t)new_proc(name,12288),next_root=(uint32_t)new_tree(name),next_thread,pid;
  unsigned char payload[512];
  CHECK(next_owner && next_root,1,"stale generation probe owns a fresh original process/tree");
  CHECK(dc_goal_activate_process(rt,next_owner,12400,next_root,stack,8192,name),next_owner,"new original activation supplies a fresh live generation");
  next_thread=word(rt,next_owner,40);pid=word(rt,next_owner,36);CHECK(pid!=good.main_import_pid,1,"fresh original activation advances process PID");
  CHECK(dc_goal_resize_main_backup(rt,next_owner,12400,next_thread,stack,8192,512),1,"stale generation has an adequate original backup");
  CHECK(dc_goal_arm_main_thread(rt,next_owner,12400,next_thread,stack,8192,target,wanted),1,"fresh generation arms unchanged original startup");
  CHECK(dc_goal_fiber_init_main(&fiber,rt,next_owner,12400,next_thread,stack,8192),1,"fresh generation imports its own receipt");
  CHECK(dc_goal_bind_process_context(rt,next_owner,12400),1,"stale-generation body uses explicit new owner identity");
  CHECK(dc_goal_fiber_step(&fiber,0,&value),DC_FIBER_YIELDED,"fresh-generation image genuinely suspends before invalidation");dc_goal_clear_process_context(rt);
  memcpy(payload,dc_goal_resolve(rt,next_thread+124,512),512);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
  {uint32_t changed=pid+1;memcpy(dc_goal_resolve(rt,next_owner+36,4),&changed,4);}
  value=UINT64_C(0x1122334455667788);rt->error=DC_GOAL_OK;
  CHECK(dc_goal_fiber_step(&fiber,99,&value),DC_FIBER_FAIL,"changed original PID is rejected before saved image restoration");
  CHECK(fiber.phase,DC_FIBER_FAULT,"stale PID closes unsupported native continuation");CHECK(value,UINT64_C(0x1122334455667788),"stale PID preserves caller output");
  CHECK(filled(dc_goal_resolve(rt,stack,8192),8192,0xd3),1,"stale PID cannot touch poisoned destination stack");
  CHECK(memcmp(payload,dc_goal_resolve(rt,next_thread+124,512),512),0,"stale PID leaves entire original backup unchanged");
  rt->error=DC_GOAL_OK;CHECK(dc_goal_main_completion_check(&fiber,rt),0,"stale PID fault cannot masquerade as completion");
  CHECK(dc_goal_fiber_clear(&fiber),1,"stale-generation terminal metadata explicitly released");
  memcpy(dc_goal_resolve(rt,next_owner+36,4),&pid,4);used=rt->used;symbols=rt->symbol_count;
 }

 CHECK(filled(dc_goal_resolve(rt,allocation,16),16,0xa5),1,"original armed startup lower stack fence");CHECK(filled(dc_goal_resolve(rt,stack+8192,16),16,0xa5),1,"original armed startup upper stack fence");
 CHECK(rt->used,used,"arming/import/yield paths allocate no arena bytes");CHECK(rt->symbol_count,symbols,"arming/import/yield paths intern no names");
restore:
 dc_goal_set_method(rt,rt->object_type,0,old_object);dc_goal_set_method(rt,proc,0,old_proc);dc_goal_set_method(rt,tree,0,old_tree);dc_goal_set_method(rt,cpu,0,old_cpu);dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
done: free(before);free(expected);
#undef CHECK
 return guard.code_bytes+module.code_bytes+probes.code_bytes;
}
