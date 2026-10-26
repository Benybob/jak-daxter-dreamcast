/* Unchanged original looping-code suspending a native fiber (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#include "fiber_test_support.h"
#include "build/original_looping_code.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*one_fn)(uint64_t);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
enum {CHILD=16,BROTHER=12,STATUS=32,PID=36,TOP_THREAD=44,MEMBER_BYTES=2160,STACK_BYTES=16384,BACKUP=1024,
      DEACTIVATE_SLOT=10,RETURN_SLOT=15,STEPS=4};
uint32_t dc_runtime_suspend_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_process_spans table;dc_goal_process_span rows[8];dc_goal_pp_scope_stack scopes;dc_goal_fiber fiber;
 dc_goal_module_result original={0};
 uint32_t type=entry(rt,"process"),tree_type=entry(rt,"process-tree"),pool_type=entry(rt,"dead-pool");
 uint32_t name=dc_goal_intern(rt,"dc-suspend-process"),ready=dc_goal_intern(rt,"ready"),dead=dc_goal_find(rt,"dead");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t old_tree=dc_goal_get_method(rt,tree_type,0),old_pool=pool_type?dc_goal_get_method(rt,pool_type,0):0;
 uint32_t old_return=pool_type?dc_goal_get_method(rt,pool_type,RETURN_SLOT):0,old_deactivate=dc_goal_get_method(rt,type,DEACTIVATE_SLOT);
 uint32_t a,pool,s,stack,thread,i,before_pid;uint64_t out;unsigned intact;
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process");
 three_fn pool_create=(three_fn)(uintptr_t)entry(rt,"dc-dead-pool-create");
 one_fn deactivate=(one_fn)(uintptr_t)entry(rt,"(method deactivate process)");zero_fn loop;
#define CHECK(x,y,label) check((uint64_t)(x),(uint64_t)(y),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(entry(rt,"dc-c-suspend")!=0,1,"suspend transfer bridge installed by the deactivation cases");
 CHECK(pool_type!=0 && deactivate!=NULL,1,"original pool type and deactivate available");
 CHECK(dc_goal_type_of(rt,entry(rt,"dc-c-suspend")),rt->function_type,"suspend import is a native function object");
 CHECK(dc_goal_find(rt,"looping-code")?dc_goal_symbol_value(rt,dc_goal_find(rt,"looping-code")):0,0,"looping-code is not yet bound");
 CHECK(dc_goal_type_of(rt,entry(rt,"string")),rt->type_type,"string type import available");
 {
  /* The original debug stack check carries its format string as module data. */
  int loaded=dc_goal_load_module_data(rt,goal_original_looping_code_exports,GOAL_ORIGINAL_LOOPING_CODE_EXPORT_COUNT,
                                      GOAL_ORIGINAL_LOOPING_CODE_DATA,GOAL_ORIGINAL_LOOPING_CODE_DATA_COUNT,
                                      sync,NULL,&original);
  /* A failure reports 100+error so the receipt names the loader's reason. */
  CHECK(loaded?1:100+rt->error,1,"unchanged original looping-code links with the suspend import");
  rt->error=DC_GOAL_OK;
 }
 if(!original.functions||!pool_type||!deactivate||!entry(rt,"dc-c-suspend"))goto restore;
 loop=(zero_fn)(uintptr_t)entry(rt,"looping-code");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"sized allocation bridge selected");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,0,entry(rt,"(method new dead-pool)")),1,"original pool constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,RETURN_SLOT,entry(rt,"(method return-process dead-pool)")),1,"original return-process selected");
 CHECK(dc_goal_set_method(rt,type,DEACTIVATE_SLOT,entry(rt,"(method deactivate process)")),1,"original deactivate selected");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,8),1,"ownership ledger registered before construction");
 a=(uint32_t)create(name,128);pool=(uint32_t)pool_create(1,2048,name);
 CHECK(a&&pool,1,"scheduler process and thread-sized original pool member constructed");if(!a||!pool)goto detach;
 s=word(rt,word(rt,pool,CHILD),0);stack=dc_goal_alloc(rt,STACK_BYTES);
 CHECK(stack!=0,1,"owned native working stack allocated");if(!stack)goto detach;
 thread=dc_goal_construct_main_thread(rt,s,MEMBER_BYTES,stack,STACK_BYTES,name,BACKUP);
 CHECK(thread!=0,1,"original main CPU thread constructed");if(!thread)goto detach;
 put(rt,s,TOP_THREAD,thread);put(rt,s,STATUS,ready);put(rt,s,PID,41);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"lexical scope metadata registered");
 /* Without a running fiber the original suspend has no kernel to return to. */
 CHECK(dc_goal_bind_process_context(rt,s,MEMBER_BYTES),1,"member bound as scheduler process");
 CHECK(loop(),0,"looping-code called directly refuses at its first suspend");
 CHECK(rt->error,DC_GOAL_UNSUPPORTED_TRANSFER,"direct suspend reports unsupported transfer");rt->error=DC_GOAL_OK;
 memset(&fiber,0,sizeof fiber);
 CHECK(dc_goal_fiber_init(&fiber,rt,s,MEMBER_BYTES,thread,stack,STACK_BYTES,entry(rt,"looping-code")),1,
       "native fiber runs the unchanged original looping-code");
 for(i=0;i<STEPS;++i){
  out=1234;intact=0;
  CHECK(dc_runtime_fiber_checked_step(&fiber,i,&out,&intact),DC_FIBER_YIELDED,"each step suspends at the original suspend");
  CHECK(intact,1,"suspension preserves root registers, FPU state and canaries");
  CHECK(out,0,"original suspend yields value zero");
  CHECK(fiber.phase,DC_FIBER_SUSPENDED,"fiber is suspended between frames");
  CHECK(rt->error,DC_GOAL_OK,"suspension is not an error");
  CHECK(word(rt,s,STATUS),dc_goal_find(rt,"suspended"),"suspension marks the process suspended like original thread-suspend");
  CHECK(dc_goal_pp_scope_depth(rt),0,"suspension holds no lexical scope");
  /* The engine restores the suspended image from the thread backup. */
  memset(dc_goal_resolve(rt,stack,STACK_BYTES),0xa5,STACK_BYTES);
 }
 CHECK(fiber.peak_requested_image_bytes<=BACKUP,1,"measured suspended image fits the original-thread backup");
 CHECK(fiber.peak_requested_image_bytes>0,1,"suspended image size is measured");
 /* Kill the suspended looping process from another process. */
 dc_goal_clear_process_context(rt);before_pid=word(rt,s,PID);
 CHECK(before_pid,41,"suspended process still has its PID");
 CHECK(dc_goal_bind_process_context(rt,a,240),1,"another scheduler process bound");
 deactivate(s);
 CHECK(rt->error,DC_GOAL_OK,"original deactivate of a suspended process succeeds");
 CHECK(word(rt,s,STATUS),dead,"suspended looping process killed");CHECK(word(rt,s,PID),0,"killed process PID cleared");
 CHECK(word(rt,pool,CHILD),s+24,"killed process returned to its original pool");
 dc_goal_clear_process_context(rt);
 CHECK(dc_goal_fiber_clear(&fiber),1,"suspended fiber of a dead process clears");
 CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"quiescent scope metadata detaches");
detach:
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);
 CHECK(dc_goal_process_spans_detach(rt),1,"ownership ledger detaches");
restore:
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object allocation method restored");
 CHECK(dc_goal_set_method(rt,type,0,old_process),1,"process constructor restored");
 CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"tree constructor restored");
 if(pool_type){
  CHECK(dc_goal_set_method(rt,pool_type,0,old_pool),1,"pool constructor restored");
  CHECK(dc_goal_set_method(rt,pool_type,RETURN_SLOT,old_return),1,"pool return restored");
 }
 CHECK(dc_goal_set_method(rt,type,DEACTIVATE_SLOT,old_deactivate),1,"deactivate slot restored");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return original.code_bytes;
}
