/* Original frontend stack objects in root/fiber calls, both modes (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "goal_fiber.h"
#include "pp_scope.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include "build/stack_objects_fixture.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*one_fn)(uint64_t);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
typedef uint64_t (*four_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*six_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*mixed_fn)(uint64_t,float);
int dc_runtime_stack_baseline_load(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
uint32_t dc_runtime_stack_objects_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result module={0};dc_goal_process_spans table;dc_goal_process_span rows[4];dc_goal_pp_scope_stack scopes;dc_goal_fiber fiber;
 uint32_t type=entry(rt,"process"),old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t a,b,owner,stack,thread,used,name=dc_goal_intern(rt,"dc-stack-owner"),ctx=entry(rt,"*kernel-context*"),record;
 uint32_t code_bytes=0;unsigned mode,i,intact;uint64_t out,argument=9;
 const uint64_t values[]={0,3,UINT64_C(0x100000000),UINT64_C(0xfedcba9876543210)};
 volatile uint32_t canary[4]={0x12345678u,0xabcdef01u,0x87654321u,0x31415926u};
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process");
#define CHECK(x,y,label) check((uint64_t)(x),(uint64_t)(y),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"stack fixture selects original sized process allocator");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"stack fixture selects original process constructor");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,4),1,"stack objects use separately owned process reservations");
 a=(uint32_t)create(name,128);b=(uint32_t)create(name,128);owner=(uint32_t)create(name,8192);
 CHECK(a!=0&&b!=0&&owner!=0,1,"original processes for stack lexical/fiber oracles");if(!a||!b||!owner)goto detach;
 put(rt,a,36,71);put(rt,b,36,72);put(rt,owner,36,73);
 stack=dc_goal_alloc(rt,16384);CHECK(stack!=0,1,"owned stack-object suspension working stack");if(!stack)goto detach;
 thread=dc_goal_construct_main_thread(rt,owner,8304,stack,16384,name,4096);
 CHECK(thread!=0,1,"original CPU thread has ample exact-image backup for stack fixtures");if(!thread)goto detach;
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"stack/lexical cleanup uses existing authenticated scope metadata");
 for(mode=0;mode<2;++mode){
  int loaded=mode?dc_runtime_stack_baseline_load(rt,sync,&module):dc_goal_load_module_data(rt,
   goal_stack_objects_fixture_exports,GOAL_STACK_OBJECTS_FIXTURE_EXPORT_COUNT,
   GOAL_STACK_OBJECTS_FIXTURE_DATA,GOAL_STACK_OBJECTS_FIXTURE_DATA_COUNT,sync,NULL,&module);
  CHECK(loaded,1,"native stack fixture code/data loads in each optimization mode");if(!loaded)goto scope_detach;
  code_bytes+=module.code_bytes;
  if(!mode)CHECK(dc_goal_run_initializer(rt,"dc-stack-fixture-init",&out),1,"original deftype/method initializer installs boxed stack record");
  else CHECK(dc_goal_set_method(rt,entry(rt,"dc-stack-record"),0,entry(rt,"(method new dc-stack-record)")),1,"baseline boxed constructor uses same original type layout");
  record=entry(rt,"dc-stack-record");CHECK(dc_goal_type_of(rt,record),rt->type_type,"boxed stack record uses actual original type metadata");
  used=rt->used;
  for(i=0;i<4;++i)CHECK(((two_fn)(uintptr_t)entry(rt,"dc-stack-distinct"))(values[i],values[i]+2),4*values[i]+7,"two complete stack arrays remain distinct across 64-bit values");
  CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-stack-alignment"))(),0,"original aligned stack slot0 is sixteen-byte aligned");
  CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-stack-high-word"))(),0,"native P0/P1 stack address is zero-extended in scalar container");
  CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-stack-bounds"))(),15,"first/last fields of both arrays stay inside independent reservations");
  CHECK(((one_fn)(uintptr_t)entry(rt,"dc-stack-boxed"))(UINT64_C(0xfedcba9876543210)),UINT64_C(0xfedcba9876543211),"new stack boxed object executes original typed constructor and field accesses");
  CHECK(((four_fn)(uintptr_t)entry(rt,"dc-stack-outgoing"))(10,20,30,40),107,"outgoing stack arguments do not overwrite local object");
  CHECK(((mixed_fn)(uintptr_t)entry(rt,"dc-stack-mixed"))(10,1.0f),45,"mixed integer/float calls preserve live stack object fields");
  CHECK(((six_fn)(uintptr_t)entry(rt,"dc-stack-spill"))(1,2,3,4,5,6),121,"six live scalar values and object area remain disjoint");
  CHECK(rt->used,used,"stack objects allocate no runtime arena bytes");
  CHECK(canary[0]==0x12345678u&&canary[1]==0xabcdef01u&&canary[2]==0x87654321u&&canary[3]==0x31415926u,1,"root C stack canaries survive all authored stack shapes");
  CHECK(dc_goal_bind_process_context(rt,a,240),1,"scheduler A explicitly bound before stack lexical bracket");
  CHECK(((three_fn)(uintptr_t)entry(rt,"dc-stack-lexical"))(b,7,entry(rt,"dc-stack-plus-one")),15,"stack object survives entered lexical process and valid callback");
  CHECK(dc_goal_pp_scope_depth(rt),0,"normal stack lexical call restores scope depth");
  CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"))(),a,"normal stack lexical call restores pp A");
  CHECK(((three_fn)(uintptr_t)entry(rt,"dc-stack-lexical"))(b,0,0),0,"invalid callback takes existing call-fault cleanup with zero stack value");
  CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"stack lexical call retains explicit invalid-function fault");
  CHECK(dc_goal_pp_scope_depth(rt),0,"stack object cannot overwrite lexical cleanup flag on fault");
  CHECK(word(rt,ctx,20),a,"faulted stack lexical call preserves scheduler A");
  rt->error=DC_GOAL_OK;CHECK(((zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"))(),a,"fault cleanup restores execution and allocation pairing");
  dc_goal_clear_process_context(rt);put(rt,owner,32,dc_goal_find(rt,"running"));memset(&fiber,0,sizeof fiber);
  CHECK(dc_goal_fiber_init_words(&fiber,rt,owner,8304,thread,stack,16384,entry(rt,"dc-stack-suspend"),1,&argument),1,"native stack-local fixture enters with exact scalar prototype");
  CHECK(dc_goal_bind_process_context(rt,owner,8304),1,"stack suspension owner explicitly bound");
  out=1234;CHECK(dc_runtime_fiber_checked_step(&fiber,0,&out,&intact),DC_FIBER_YIELDED,"original suspend yields with live array in native stack image");
  CHECK(intact,1,"stack object yield preserves root integer/FP registers and canaries");
  CHECK(out,0,"original suspend returns its native yielded value");
  CHECK(fiber.image.image_bytes>0&&fiber.image.image_bytes<=4096,1,"live object is backed up inside the exact original CPU stack image");
  memset(dc_goal_resolve(rt,stack,16384),0x6d,16384);
  CHECK(dc_runtime_fiber_checked_step(&fiber,0,&out,&intact),DC_FIBER_COMPLETED,"same fiber restores stack objects after complete working-stack destruction");
  CHECK(intact,1,"restored stack object completion preserves root integer/FP state");
  CHECK(out,25,"both 64-bit object fields survive original suspend and native image restore");
  CHECK(dc_goal_fiber_clear(&fiber),1,"completed stack-object fiber clears without stale metadata");
  dc_goal_clear_process_context(rt);CHECK(rt->error,DC_GOAL_OK,"each stack optimization mode leaves clean native state");
 }
scope_detach:
 dc_goal_clear_process_context(rt);CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"stack oracle scope metadata detaches");
detach:
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);CHECK(dc_goal_process_spans_detach(rt),1,"stack fixture process ownership detaches");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"original allocator method restored after stack oracles");
 CHECK(dc_goal_set_method(rt,type,0,old_process),1,"original process method restored after stack oracles");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return code_bytes;
}
