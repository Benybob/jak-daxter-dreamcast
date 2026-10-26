/* Execute unchanged original initialization/catch/throw on root SH4 stack (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#include "fiber_test_support.h"
#include "build/original_initialization.h"
#include "build/initialization_fixture.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*eight_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*five_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned o){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+o,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned o,uint32_t v){memcpy(dc_goal_resolve(r,p+o,4),&v,4);}
static dc_goal_runtime *exit_runtime;static unsigned exits;static uint32_t exit_process;
static unsigned fault_mode,fault_entered;
static unsigned fault_storage_ok;
static uint64_t observe_exit(void){++exits;exit_process=(uint32_t)((zero_fn)(uintptr_t)entry(exit_runtime,"dc-c-current-process"))();return 0;}
static uint64_t cause_fault(void){
 dc_goal_runtime *rt=exit_runtime;uint32_t p=(uint32_t)((zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"))();
 dc_goal_process_span span;fault_entered=1;
 if(fault_mode==0)((two_fn)(uintptr_t)entry(rt,"throw-dispatch"))(UINT64_MAX,7);
 else if(fault_mode==1)put(rt,p,36,word(rt,p,36)+1);
 else if(fault_mode==2)dc_goal_clear_process_context(rt);
 else if(fault_mode==3){if(dc_goal_process_span_lookup(rt,p,&span))dc_goal_process_span_retire(rt,&span);}
 else if(fault_mode==4){uint32_t frame=word(rt,p,88),wrong=rt->s7;memcpy((void *)(uintptr_t)frame,&wrong,4);}
 else {
  unsigned char raw[176] __attribute__((aligned(16))),before[176];uint64_t args[6]={0};
  memset(raw,0x7d,sizeof raw);memcpy(before,raw,sizeof raw);
  ((five_fn)(uintptr_t)entry(rt,"dc-c-catch-frame-new"))((uintptr_t)raw,entry(rt,"catch-frame"),
   dc_goal_find(rt,"initialize"),entry(rt,"dc-initialize-return"),(uintptr_t)args);
  fault_storage_ok=memcmp(raw,before,sizeof raw)==0;
 }
 return 7;
}
uint32_t dc_runtime_initialization_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result bridge={0},original={0},callbacks={0},observer={0};dc_goal_process_spans spans;
 dc_goal_process_span rows[4];dc_goal_pp_scope_stack scopes;
 uint32_t process_type=entry(rt,"process"),protect=entry(rt,"protect-frame"),a,b,used,code=0;
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,process_type,0),old_protect;
 uint32_t tree=entry(rt,"process-tree"),pool_type=entry(rt,"dead-pool"),pool,member;
 uint32_t old_tree=dc_goal_get_method(rt,tree,0),old_pool=dc_goal_get_method(rt,pool_type,0);
 uint32_t old_return=dc_goal_get_method(rt,pool_type,15),old_deactivate=dc_goal_get_method(rt,process_type,10);
 uint32_t target,ctx=entry(rt,"*kernel-context*"),address=(uint32_t)(uintptr_t)observe_exit;
 unsigned path,v;uint64_t result,x,expected,arg5;
 const uint64_t patterns[]={0,UINT64_C(0x100000000),UINT64_C(0xfedcba9876543210),UINT64_MAX};
 const char *names[]={"dc-initialize-return","dc-initialize-throw","dc-initialize-protect","dc-initialize-nested","dc-initialize-scoped"};
 unsigned char observer_code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 unsigned char fault_code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export export={"dc-initialize-observer",observer_code,12,NULL,0};
 dc_goal_code_export fault_export={"dc-initialize-fault",fault_code,12,NULL,0};
 volatile uint32_t canary[4]={0x11223344,0xaabbccdd,0x55667788,0xdeadbeef};
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process");eight_fn run;
#define CHECK(a,b,s) check((uint64_t)(a),(uint64_t)(b),++*id,s,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_install_catch_bridge(rt,sync,&bridge),1,"explicit root lexical catch bridges and exact original catch type install");code+=bridge.code_bytes;
 CHECK(dc_goal_load_module_data(rt,goal_original_initialization_exports,GOAL_ORIGINAL_INITIALIZATION_EXPORT_COUNT,
  GOAL_ORIGINAL_INITIALIZATION_DATA,GOAL_ORIGINAL_INITIALIZATION_DATA_COUNT,sync,NULL,&original),1,"unchanged run-function-in-process throw and protect constructor link");code+=original.code_bytes;
 CHECK(dc_goal_load_module_data(rt,goal_initialization_fixture_exports,GOAL_INITIALIZATION_FIXTURE_EXPORT_COUNT,
  GOAL_INITIALIZATION_FIXTURE_DATA,GOAL_INITIALIZATION_FIXTURE_DATA_COUNT,sync,NULL,&callbacks),1,"six-argument native initialization callbacks link");code+=callbacks.code_bytes;
 memcpy(observer_code+8,&address,4);
 CHECK(dc_goal_load_module(rt,&export,1,sync,NULL,&observer),1,"native observer records original protect exit process");code+=observer.code_bytes;
 address=(uint32_t)(uintptr_t)cause_fault;memcpy(fault_code+8,&address,4);
 {dc_goal_module_result loaded={0};CHECK(dc_goal_load_module(rt,&fault_export,1,sync,NULL,&loaded),1,"fault oracle registers an exact native callback entry");code+=loaded.code_bytes;}
 if(!bridge.functions||!original.functions||!callbacks.functions||!observer.functions)return code;
 old_protect=dc_goal_get_method(rt,protect,0);
 CHECK(((dc_goal_type_fields *)dc_goal_resolve(rt,protect,sizeof(dc_goal_type_fields)))->allocated_size,16,"protect layout matches original frontend flags not earlier padded fixture guess");
 CHECK(dc_goal_set_method(rt,protect,0,entry(rt,"(method new protect-frame)")),1,"original typed protect constructor selected");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"original initialization owners use sized global allocation");
 CHECK(dc_goal_set_method(rt,process_type,0,entry(rt,"(method new process)")),1,"original process constructor selected for root initialization");
 CHECK(dc_goal_set_method(rt,tree,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected for initializer pool");
 CHECK(dc_goal_set_method(rt,pool_type,0,entry(rt,"(method new dead-pool)")),1,"original fixed pool constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,15,entry(rt,"(method return-process dead-pool)")),1,"original fixed pool return installed for initialize-dead");
 CHECK(dc_goal_set_method(rt,process_type,10,entry(rt,"(method deactivate process)")),1,"original deactivate selected for normal initializer death");
 CHECK(dc_goal_register_process_spans(rt,&spans,rows,4),1,"root initializers have explicit physical process owners");
 a=(uint32_t)create(dc_goal_intern(rt,"dc-init-scheduler"),1024);b=(uint32_t)create(dc_goal_intern(rt,"dc-init-target"),1024);
 CHECK(a&&b,1,"two original processes created before binding any execution identity");if(!a||!b)goto detach;
 put(rt,a,36,81);put(rt,b,36,82);
 pool=(uint32_t)((three_fn)(uintptr_t)entry(rt,"dc-dead-pool-create"))(1,256,dc_goal_intern(rt,"dc-init-pool"));
 CHECK(pool!=0,1,"original fixed pool provides owned member for initialize-dead branch");
 if(!pool)goto detach;
 member=word(rt,word(rt,pool,16),0);put(rt,member,36,83);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"root initialization has registered authenticated lexical scopes");
 CHECK(dc_goal_bind_process_context(rt,a,1136),1,"scheduler A explicitly bound before original initialization of B");
 run=(eight_fn)(uintptr_t)entry(rt,"run-function-in-process");exit_runtime=rt;
 CHECK(((two_fn)(uintptr_t)entry(rt,"change-parent"))(member,a),member,"initializer member detaches from pool into scheduler tree through original code");
 result=run(member,entry(rt,"dc-initialize-die"),UINT64_C(0x100000000),3,5,7,11,19);
 CHECK(result,UINT64_C(0x10000002d),"unchanged normal initializer retains full result through original deactivation");
 CHECK(rt->error,DC_GOAL_OK,"original initialize-dead branch returns without native transfer fault");
 CHECK(word(rt,member,32),dc_goal_find(rt,"dead"),"normal original initializer deactivates rather than pretending it entered a state");
 CHECK(word(rt,member,36),0,"original initialize-dead clears member PID");
 CHECK(word(rt,member,88),rt->s7,"deactivated initializer has no expired catch frame");
 CHECK(word(rt,pool,16),member+24,"original return-process returns completed initializer to fixed pool");
 CHECK(word(rt,a,16),rt->s7,"original deactivation removes initializer from active tree");
 CHECK(dc_goal_pp_scope_depth(rt),0,"original initializer death restores all nested lexical scopes");
 CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"))(),a,"initializer death preserves live scheduler A");
 for(path=0;path<5;++path)for(v=0;v<4;++v){
  x=patterns[v];target=entry(rt,names[path]);arg5=path==2?entry(rt,"dc-initialize-observer"):path==4?b:19;
  exits=0;exit_process=0;used=rt->used;
  result=run(b,target,x,3,5,7,11,arg5);expected=x+26+(path==2||path==4?0:19);
  CHECK(result,expected,"unchanged original initialization returns full six-argument normal/throw result");
  CHECK(rt->error,DC_GOAL_OK,"original initialization completes without native fault");
  CHECK(word(rt,b,32),dc_goal_find(rt,"waiting-to-run"),"original initialization changes initialize-go to waiting-to-run");
  CHECK(word(rt,b,88),rt->s7,"native catch and original protect traversal restore previous frame chain");
  CHECK(dc_goal_pp_scope_depth(rt),0,"original initialization restores lexical scope depth");
  CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"))(),a,"original old-pp bracket restores scheduler process A");
  CHECK(word(rt,ctx,20),a,"original initialization never changes scheduler context current-process");
  CHECK(exits,path==2,"original throw executes exactly one protect exit before dispatch");
  CHECK(exit_process,path==2?b:0,"original protect exit observes target lexical process B");
  CHECK(rt->used,used,"stack catch and parameters consume no arena allocation");
  CHECK(canary[0]==0x11223344&&canary[1]==0xaabbccdd&&canary[2]==0x55667788&&canary[3]==0xdeadbeef,1,"original root initializer preserves caller native stack canaries");
 }
 for(fault_mode=0;fault_mode<5;++fault_mode){
  fault_entered=0;result=run(b,entry(rt,"dc-initialize-error"),0,0,0,0,0,entry(rt,"dc-initialize-fault"));
  CHECK(result,0,"faulted original initialization refuses a success result");
  CHECK(fault_entered,1,"fault callback really executes inside live native catch");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"invalid dispatch PID frame mutation context clear and retirement retain explicit fault");
  CHECK(word(rt,b,88),rt->s7,"faulted catch never leaves its expired native frame in the process chain");
  CHECK(dc_goal_pp_scope_depth(rt),0,"original call-fault cleanup restores lexical depth even with changed PID");
  CHECK(word(rt,ctx,20),a,"faulted initialization preserves scheduler process identity");
  put(rt,b,36,82);rt->error=DC_GOAL_OK;
  CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"))(),a,"explicit owner remains usable after faulted initializer cleanup");
 }
 {
  unsigned char raw[176] __attribute__((aligned(16))),before[176],process_before[1136];
  uint64_t args[6]={0};five_fn ctor=(five_fn)(uintptr_t)entry(rt,"dc-c-catch-frame-new");
  uint32_t catch_type=entry(rt,"catch-frame"),name=dc_goal_intern(rt,"initialize");
  memset(raw,0x5b,sizeof raw);memcpy(before,raw,sizeof raw);
  CHECK(dc_goal_enter_known_scope(rt,b),1,"negative catch arguments tested under real lexical process B");
  memcpy(process_before,dc_goal_resolve(rt,b-4,1136),1136);
  for(v=0;v<9;++v){
   uint64_t allocation=(uintptr_t)raw,param=(uintptr_t)args,type=catch_type,fn=entry(rt,"dc-initialize-return");
   if(v==0)allocation+=1;else if(v==1)allocation=rt->base;else if(v==2)param=allocation;
   else if(v==3)param+=1;else if(v==4)fn=0;else if(v==5)type=rt->basic_type;
   else if(v==6)allocation|=UINT64_C(1)<<40;
   else if(v==7)allocation=(allocation&0x1fffffffu)|0xe0000000u;
   else param=(param&0x1fffffffu)|0xa0000000u;
   CHECK(ctor(allocation,type,name,fn,param),0,"unsupported stack spans shapes type and entry reject before any body execution");
   CHECK(rt->error,v==4?DC_GOAL_BAD_FUNCTION:DC_GOAL_BAD_ARGUMENT,"invalid catch arguments retain exact span/type or code-entry fault");
   CHECK(memcmp(raw,before,sizeof raw),0,"rejected catch leaves complete native object reservation unchanged");
   CHECK(memcmp(dc_goal_resolve(rt,b-4,1136),process_before,1136),0,"rejected catch leaves original process and heap bytes unchanged");
   CHECK(dc_goal_pp_scope_depth(rt),1,"rejected catch does not consume its caller's lexical bracket");
   rt->error=DC_GOAL_OK;
  }
  CHECK(dc_goal_leave_known_scope(rt,a),1,"negative catch oracle leaves its explicit original process bracket");
 }
 {
  dc_goal_fiber fiber;uint32_t stack,thread;unsigned intact;uint64_t out=123;
  dc_goal_clear_process_context(rt);stack=dc_goal_alloc(rt,16384);
  CHECK(stack!=0,1,"unsupported-fiber catch oracle has explicit owned native working stack");
  thread=dc_goal_construct_main_thread(rt,b,1136,stack,16384,dc_goal_intern(rt,"dc-init-unsupported"),256);
  CHECK(thread!=0,1,"original CPU thread backs unsupported catch context oracle");
  memset(&fiber,0,sizeof fiber);fault_mode=5;fault_entered=0;fault_storage_ok=0;
  CHECK(dc_goal_fiber_init(&fiber,rt,b,1136,thread,stack,16384,entry(rt,"dc-initialize-fault")),1,"native fiber entry uses its declared zero-argument C callback ABI");
  CHECK(dc_goal_bind_process_context(rt,b,1136),1,"unsupported catch oracle explicitly binds scheduler B");
  CHECK(dc_runtime_fiber_checked_step(&fiber,0,&out,&intact),DC_FIBER_FAIL,"root-only catch adapter refuses running fiber without installing an anchor");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"unsupported fiber catch leaves an explicit retained fault");
  CHECK(fault_storage_ok,1,"unsupported fiber catch does not write even its valid native stack reservation");
  CHECK(fault_entered,1,"unsupported catch was attempted inside actual native fiber execution");
  CHECK(out,123,"faulted unsupported catch does not manufacture a native completion value");
  CHECK(intact,1,"unsupported catch rejection preserves root integer floating registers and canaries");
  CHECK(dc_goal_pp_scope_depth(rt),0,"unsupported fiber catch does not create lexical scope metadata");
  CHECK(word(rt,b,88),rt->s7,"unsupported fiber catch leaves process frame chain untouched");
  CHECK(dc_goal_fiber_clear(&fiber),1,"faulted unsupported catch fiber clears without a stale active descriptor");
  dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 }
 dc_goal_clear_process_context(rt);CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"initialization scope owner detaches after all original returns");
detach:
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);CHECK(dc_goal_process_spans_detach(rt),1,"original initialization process owners detach");
 CHECK(dc_goal_set_method(rt,protect,0,old_protect),1,"protect constructor restored after original initialization oracles");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"sized allocator method restored after initialization");
 CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor restored after initialization");
 CHECK(dc_goal_set_method(rt,tree,0,old_tree),1,"tree constructor restored after initialization");
 CHECK(dc_goal_set_method(rt,pool_type,0,old_pool),1,"pool constructor restored after initialization");
 CHECK(dc_goal_set_method(rt,pool_type,15,old_return),1,"pool return method restored after initialization");
 CHECK(dc_goal_set_method(rt,process_type,10,old_deactivate),1,"deactivate method restored after initialization");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return code;
}
