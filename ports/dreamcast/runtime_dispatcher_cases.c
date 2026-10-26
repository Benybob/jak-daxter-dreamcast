/* Unchanged original kernel-dispatcher scheduling a native fiber (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#include "build/original_dispatcher.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*one_fn)(uint64_t);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
enum {MASK=4,CHILD=16,STATUS=32,PID=36,MAIN=40,TOP=44,TRANS_HOOK=56,POST_HOOK=60,RESUME_HOOK=16,
      MEMBER_BYTES=2160,STACK_BYTES=16384,BACKUP=1024,DEACTIVATE_SLOT=10,RUN_LOGIC_SLOT=12,RETURN_SLOT=15,FRAMES=4};
static void global(dc_goal_runtime *rt,const char *name,uint32_t value){dc_goal_set_symbol(rt,dc_goal_intern(rt,name),value);}
uint32_t dc_runtime_dispatcher_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_process_spans table;dc_goal_process_span rows[8];dc_goal_pp_scope_stack scopes;dc_goal_fiber fiber;
 dc_goal_module_result original={0};
 uint32_t type=entry(rt,"process"),tree_type=entry(rt,"process-tree"),pool_type=entry(rt,"dead-pool");
 uint32_t name=dc_goal_intern(rt,"dc-dispatch-process"),waiting=dc_goal_intern(rt,"waiting-to-run");
 uint32_t suspended=dc_goal_intern(rt,"suspended"),dead=dc_goal_find(rt,"dead");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t old_tree=dc_goal_get_method(rt,tree_type,0),old_pool=pool_type?dc_goal_get_method(rt,pool_type,0):0;
 uint32_t old_return=pool_type?dc_goal_get_method(rt,pool_type,RETURN_SLOT):0;
 uint32_t old_deactivate=dc_goal_get_method(rt,type,DEACTIVATE_SLOT),old_run=dc_goal_get_method(rt,type,RUN_LOGIC_SLOT);
 uint32_t a,pool,root,s,stack,thread,i,ctx=entry(rt,"*kernel-context*");
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process"),move=(two_fn)(uintptr_t)entry(rt,"change-parent");
 one_fn new_tree=(one_fn)(uintptr_t)entry(rt,"dc-new-tree");
 three_fn pool_create=(three_fn)(uintptr_t)entry(rt,"dc-dead-pool-create");
 one_fn deactivate=(one_fn)(uintptr_t)entry(rt,"(method deactivate process)");zero_fn dispatch;
#define CHECK(x,y,label) check((uint64_t)(x),(uint64_t)(y),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(entry(rt,"dc-c-thread-resume")!=0 && entry(rt,"looping-code")!=0,1,"native thread-resume and original looping-code available");
 {
  int loaded=dc_goal_load_module_data(rt,goal_original_dispatcher_exports,GOAL_ORIGINAL_DISPATCHER_EXPORT_COUNT,
                                      GOAL_ORIGINAL_DISPATCHER_DATA,GOAL_ORIGINAL_DISPATCHER_DATA_COUNT,sync,NULL,&original);
  CHECK(loaded?1:100+rt->error,1,"unchanged original kernel-dispatcher links under the Dreamcast profiler context");
  rt->error=DC_GOAL_OK;
 }
 if(!original.functions||!entry(rt,"dc-c-thread-resume")||!pool_type||!deactivate)goto restore;
 dispatch=(zero_fn)(uintptr_t)entry(rt,"kernel-dispatcher");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"sized allocation bridge selected");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,0,entry(rt,"(method new dead-pool)")),1,"original pool constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,RETURN_SLOT,entry(rt,"(method return-process dead-pool)")),1,"original return-process selected");
 CHECK(dc_goal_set_method(rt,type,DEACTIVATE_SLOT,entry(rt,"(method deactivate process)")),1,"original deactivate selected");
 CHECK(dc_goal_set_method(rt,type,RUN_LOGIC_SLOT,entry(rt,"(method run-logic? process)")),1,"original run-logic? selected");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,8),1,"ownership ledger registered");
 a=(uint32_t)create(name,128);pool=(uint32_t)pool_create(1,2048,name);root=(uint32_t)new_tree(name);
 CHECK(a&&pool&&root,1,"scheduler helper, original pool member and active tree constructed");if(!a||!pool||!root)goto detach;
 s=word(rt,word(rt,pool,CHILD),0);stack=dc_goal_alloc(rt,STACK_BYTES);
 CHECK(stack!=0,1,"owned native working stack allocated");if(!stack)goto detach;
 thread=dc_goal_construct_main_thread(rt,s,MEMBER_BYTES,stack,STACK_BYTES,name,BACKUP);
 CHECK(thread!=0,1,"original main CPU thread constructed");if(!thread)goto detach;
 CHECK(move(s,root),s,"original change-parent attaches the process to the active tree");
 put(rt,s,MASK,0);put(rt,s,MAIN,thread);put(rt,s,TOP,thread);put(rt,s,PID,61);
 put(rt,s,TRANS_HOOK,rt->s7);put(rt,s,POST_HOOK,rt->s7);put(rt,thread,RESUME_HOOK,entry(rt,"dc-c-thread-resume"));
 global(rt,"*active-pool*",root);global(rt,"*listener-function*",rt->s7);
 global(rt,"*stdcon0*",name);global(rt,"*stdcon1*",name);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"lexical scope metadata registered");
 memset(&fiber,0,sizeof fiber);
 CHECK(dc_goal_fiber_init(&fiber,rt,s,MEMBER_BYTES,thread,stack,STACK_BYTES,entry(rt,"looping-code")),1,
       "process main thread is a native fiber running original looping-code");
 CHECK(dc_goal_register_thread_fiber(rt,&fiber),1,"fiber registered for the original thread's resume-hook");
 CHECK(dc_goal_register_thread_fiber(rt,&fiber),0,"duplicate thread registration refuses");rt->error=DC_GOAL_OK;
 for(i=0;i<FRAMES;++i){
  put(rt,s,STATUS,i?suspended:waiting);
  dispatch();
  CHECK(rt->error,DC_GOAL_OK,"original kernel-dispatcher frame completes without error");
  CHECK(word(rt,s,STATUS),suspended,"each frame resumes the process, which suspends again");
  CHECK(fiber.phase,DC_FIBER_SUSPENDED,"the main-thread fiber is suspended between frames");
  CHECK(word(rt,ctx,20),rt->s7,"original dispatcher clears current-process after the frame");
  CHECK(word(rt,s,TOP),thread,"resume installs the main thread as top-thread");
  CHECK(dc_goal_pp_scope_depth(rt),0,"no lexical scope survives the frame");
  memset(dc_goal_resolve(rt,stack,STACK_BYTES),0xa5,STACK_BYTES);
 }
 /* A process the dispatcher does not select is never resumed. */
 put(rt,s,STATUS,dc_goal_intern(rt,"ready"));dispatch();
 CHECK(word(rt,s,STATUS),dc_goal_find(rt,"ready"),"a ready (not waiting) process is not resumed");
 CHECK(rt->error,DC_GOAL_OK,"skipped process leaves no error");
 /* Without a registered fiber the native resume-hook refuses explicitly. */
 CHECK(dc_goal_unregister_thread_fiber(rt,&fiber),1,"fiber unregistered");
 put(rt,s,STATUS,suspended);dispatch();
 CHECK(rt->error,DC_GOAL_UNSUPPORTED_TRANSFER,"unregistered resume reports unsupported transfer");rt->error=DC_GOAL_OK;
 CHECK(fiber.phase,DC_FIBER_SUSPENDED,"unregistered fiber is untouched");
 /* Kill the process; the dispatcher then skips the dead process. */
 CHECK(dc_goal_register_thread_fiber(rt,&fiber),1,"fiber registered again");
 CHECK(dc_goal_bind_process_context(rt,a,240),1,"another scheduler process bound to kill it");
 deactivate(s);dc_goal_clear_process_context(rt);
 CHECK(word(rt,s,STATUS),dead,"original deactivate kills the scheduled process");
 dispatch();
 CHECK(rt->error,DC_GOAL_OK,"dispatcher frame over a dead process succeeds");
 CHECK(fiber.phase,DC_FIBER_SUSPENDED,"a dead process's fiber is never resumed");
 CHECK(dc_goal_unregister_thread_fiber(rt,&fiber),1,"fiber unregistered before clearing");
 CHECK(dc_goal_fiber_clear(&fiber),1,"suspended fiber of the dead process clears");
 CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"quiescent scope metadata detaches");
detach:
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);global(rt,"*active-pool*",rt->s7);
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
 CHECK(dc_goal_set_method(rt,type,RUN_LOGIC_SLOT,old_run),1,"run-logic? slot restored");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return original.code_bytes;
}
