/* Unchanged original enter-state restarting a native fiber at state code (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#include "fiber_test_support.h"
#include "build/original_enter_state.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
typedef uint64_t (*six_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
enum {CHILD=16,STATUS=32,PID=36,MAIN=40,TOP=44,STATE=52,TRANS_HOOK=56,POST_HOOK=60,EVENT_HOOK=64,NEXT_STATE=72,
      FRAMES=88,MEMBER_BYTES=2160,STACK_BYTES=16384,BACKUP=1024,DEACTIVATE_SLOT=10,RETURN_SLOT=15,
      ST_EXIT=8,ST_CODE=12,ST_TRANS=16,ST_POST=20,ST_ENTER=24,ST_EVENT=28};
static dc_goal_runtime *state_rt;
static uint32_t body_started,body_after,code_calls,code_pp,code_state,code_frames,enter_calls,trans_calls;
static uint64_t code_args[4],enter_args[6];
static uintptr_t enter_sp,code_sp;
static const uint64_t ARGS[6]={0x1111,0x2222,0x3333,0x4444,0x5555,0x6666};
static uint64_t state_code(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3){
 volatile uint32_t marker=0;
 code_sp=(uintptr_t)&marker;++code_calls;
 code_args[0]=a0;code_args[1]=a1;code_args[2]=a2;code_args[3]=a3;
 code_pp=(uint32_t)((zero_fn)(uintptr_t)entry(state_rt,"dc-c-current-process"))();
 /* Observe while running: return-from-thread-dead clears state afterwards. */
 code_state=word(state_rt,code_pp,STATE);code_frames=word(state_rt,code_pp,FRAMES);
 return 55;
}
static uint64_t state_enter(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){
 volatile uint32_t marker=0;
 enter_sp=(uintptr_t)&marker;++enter_calls;
 enter_args[0]=a0;enter_args[1]=a1;enter_args[2]=a2;enter_args[3]=a3;enter_args[4]=a4;enter_args[5]=a5;
 return 0;
}
static uint64_t state_trans(void){++trans_calls;return 0;}
static uint64_t enter_body(void){
 body_started=1;
 ((six_fn)(uintptr_t)entry(state_rt,"enter-state"))(ARGS[0],ARGS[1],ARGS[2],ARGS[3],ARGS[4],ARGS[5]);
 body_after=1;return 99;
}
uint32_t dc_runtime_enter_state_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_process_spans table;dc_goal_process_span rows[8];dc_goal_pp_scope_stack scopes;dc_goal_fiber fiber;
 dc_goal_module_result original={0},hooks={0};
 unsigned char code[4][12];
 dc_goal_code_export exports[4]={{"dc-state-code",code[0],12,NULL,0},{"dc-state-enter",code[1],12,NULL,0},
                                 {"dc-state-trans",code[2],12,NULL,0},{"dc-state-body",code[3],12,NULL,0}};
 const unsigned char trampoline[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 uint32_t targets[4]={(uint32_t)(uintptr_t)state_code,(uint32_t)(uintptr_t)state_enter,
                      (uint32_t)(uintptr_t)state_trans,(uint32_t)(uintptr_t)enter_body};
 uint32_t type=entry(rt,"process"),tree_type=entry(rt,"process-tree"),pool_type=entry(rt,"dead-pool");
 uint32_t name=dc_goal_intern(rt,"dc-state-process"),ready=dc_goal_intern(rt,"ready"),dead=dc_goal_find(rt,"dead");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t old_tree=dc_goal_get_method(rt,tree_type,0),old_pool=pool_type?dc_goal_get_method(rt,pool_type,0):0;
 uint32_t old_return=pool_type?dc_goal_get_method(rt,pool_type,RETURN_SLOT):0,old_deactivate=dc_goal_get_method(rt,type,DEACTIVATE_SLOT);
 uint32_t pool,s,stack,thread,raw,st,i;uint64_t out=1234;unsigned intact=0;
 three_fn pool_create=(three_fn)(uintptr_t)entry(rt,"dc-dead-pool-create");
#define CHECK(x,y,label) check((uint64_t)(x),(uint64_t)(y),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;state_rt=rt;
 CHECK(entry(rt,"dc-c-enter-state-code")!=0,1,"state-code transfer bridge installed");
 CHECK(entry(rt,"(method deactivate process)")!=0 && pool_type!=0,1,"original deactivate and pool available");
 {
  int loaded=dc_goal_load_module_data(rt,goal_original_enter_state_exports,GOAL_ORIGINAL_ENTER_STATE_EXPORT_COUNT,
                                      GOAL_ORIGINAL_ENTER_STATE_DATA,GOAL_ORIGINAL_ENTER_STATE_DATA_COUNT,sync,NULL,&original);
  CHECK(loaded?1:100+rt->error,1,"unchanged original enter-state links with transfer imports");rt->error=DC_GOAL_OK;
 }
 for(i=0;i<4;++i){memcpy(code[i],trampoline,12);memcpy(code[i]+8,&targets[i],4);}
 CHECK(dc_goal_load_module(rt,exports,4,sync,NULL,&hooks),1,"native state code/enter/trans and body registered");
 if(!original.functions||!hooks.functions||!pool_type||!entry(rt,"dc-c-enter-state-code"))goto restore;
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"sized allocation bridge selected");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,0,entry(rt,"(method new dead-pool)")),1,"original pool constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,RETURN_SLOT,entry(rt,"(method return-process dead-pool)")),1,"original return-process selected");
 CHECK(dc_goal_set_method(rt,type,DEACTIVATE_SLOT,entry(rt,"(method deactivate process)")),1,"original deactivate selected");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,8),1,"ownership ledger registered");
 pool=(uint32_t)pool_create(1,2048,name);CHECK(pool!=0,1,"thread-sized original pool member constructed");if(!pool)goto detach;
 s=word(rt,word(rt,pool,CHILD),0);stack=dc_goal_alloc(rt,STACK_BYTES);raw=dc_goal_alloc(rt,64);
 CHECK(stack&&raw,1,"working stack and state storage allocated");if(!stack||!raw)goto detach;
 thread=dc_goal_construct_main_thread(rt,s,MEMBER_BYTES,stack,STACK_BYTES,name,BACKUP);
 CHECK(thread!=0,1,"original main CPU thread constructed");if(!thread)goto detach;
 /* A minimal original-layout state: no exit, so no frame is pushed. */
 memset(dc_goal_resolve(rt,raw,64),0,64);st=raw+16;
 put(rt,st,0,name);put(rt,st,4,rt->s7);put(rt,st,ST_EXIT,rt->s7);
 put(rt,st,ST_CODE,entry(rt,"dc-state-code"));put(rt,st,ST_TRANS,entry(rt,"dc-state-trans"));
 put(rt,st,ST_POST,entry(rt,"dc-state-trans"));put(rt,st,ST_ENTER,entry(rt,"dc-state-enter"));
 put(rt,st,ST_EVENT,entry(rt,"dc-state-code"));
 put(rt,s,MAIN,thread);put(rt,s,TOP,thread);put(rt,s,STATUS,ready);put(rt,s,PID,51);put(rt,s,NEXT_STATE,st);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"lexical scope metadata registered");
 CHECK(dc_goal_bind_process_context(rt,s,MEMBER_BYTES),1,"member bound as scheduler process");
 /* Directly from C there is no fiber to restart: explicit refusal. */
 CHECK(((six_fn)(uintptr_t)entry(rt,"enter-state"))(1,2,3,4,5,6),0,"direct enter-state refuses at the code transfer");
 CHECK(rt->error,DC_GOAL_UNSUPPORTED_TRANSFER,"direct transfer reports unsupported transfer");rt->error=DC_GOAL_OK;
 code_calls=enter_calls=trans_calls=body_started=body_after=0;put(rt,s,NEXT_STATE,st);put(rt,s,FRAMES,rt->s7);
 memset(&fiber,0,sizeof fiber);
 CHECK(dc_goal_fiber_init(&fiber,rt,s,MEMBER_BYTES,thread,stack,STACK_BYTES,entry(rt,"dc-state-body")),1,"native fiber initializes");
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&out,&intact),DC_FIBER_COMPLETED,"state code returns through return-from-thread-dead");
 CHECK(intact,1,"restart and completion preserve root registers, FPU state and canaries");
 CHECK(out,0,"completion carries the deactivation transfer value");
 CHECK(body_started,1,"fiber body called enter-state");CHECK(body_after,0,"enter-state never returns to its caller");
 CHECK(enter_calls,1,"state enter function called once");
 for(i=0;i<6;++i)CHECK(enter_args[i],ARGS[i],"enter function receives all six original arguments");
 CHECK(trans_calls,1,"state trans function runs before the code");
 CHECK(code_calls,1,"state code entered once");
 for(i=0;i<4;++i)CHECK(code_args[i],ARGS[i],"state code receives the four original register arguments");
 CHECK(code_pp,s,"state code runs with pp = the process");
 CHECK(code_sp>enter_sp,1,"state code runs on the reset stack, above the abandoned enter-state frames");
 CHECK(code_sp>=stack+STACK_BYTES-512 && code_sp<stack+STACK_BYTES,1,"state code starts at the top of the main-thread stack");
 CHECK(code_state,st,"original enter-state installs the new state before its code runs");
 CHECK(code_frames,rt->s7,"a state without exit pushes no stack frame");
 CHECK(word(rt,s,STATE),rt->s7,"return-from-thread-dead deactivation clears the state");
 CHECK(word(rt,s,EVENT_HOOK),entry(rt,"dc-state-code"),"state event hook installed");
 CHECK(word(rt,s,TRANS_HOOK),entry(rt,"dc-state-trans"),"state trans hook installed");
 CHECK(word(rt,s,POST_HOOK),entry(rt,"dc-state-trans"),"state post hook installed");
 CHECK(word(rt,s,STATUS),dead,"returning state code deactivates the process");
 CHECK(word(rt,s,PID),0,"deactivated process PID cleared");
 CHECK(word(rt,pool,CHILD),s+24,"deactivated process returned to its pool");
 CHECK(fiber.phase,DC_FIBER_DONE,"fiber is DONE after the state code returns");
 CHECK(rt->error,DC_GOAL_OK,"state transfer is a normal completion");
 CHECK(dc_goal_pp_scope_depth(rt),0,"no lexical scope leaks");
 CHECK(dc_goal_fiber_clear(&fiber),1,"completed fiber clears");
 dc_goal_clear_process_context(rt);
 CHECK(dc_goal_fiber_restart(rt,entry(rt,"dc-state-code"),0,NULL),0,"restart outside a running fiber refuses");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"restart outside a fiber reports bad argument");rt->error=DC_GOAL_OK;
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
 state_rt=NULL;rt->error=DC_GOAL_OK;
#undef CHECK
 return original.code_bytes+hooks.code_bytes;
}
