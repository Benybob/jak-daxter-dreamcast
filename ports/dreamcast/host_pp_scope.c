/* Independent portable scope identities/guards; no SH4 execution (AI-assisted), ISC. */
#include "pp_scope.h"
#include "goal_kernel_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dc_goal_fiber *fake_current;
dc_goal_fiber *dc_goal_fiber_current(void){return fake_current;}
static uint32_t owner_id=77;
static uint32_t owner(void){return owner_id;}
static unsigned total,passed;
#define CHECK(x) do {++total;if(x)++passed;else fprintf(stderr,"FAIL%u line%u\n",total,(unsigned)__LINE__);}while(0)
typedef struct {
 dc_goal_runtime r;dc_goal_pp_scope_stack stack;dc_goal_process_heap allocation;
 void *memory;uint32_t a,b,c,context,symbol;dc_goal_pp_owner_hooks hooks;
} fixture;
static uint32_t word(fixture *f,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(&f->r,p+off,4),4);return v;}
static void put(fixture *f,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(&f->r,p+off,4),&v,4);}
static uint32_t process(fixture *f,uint32_t type,uint32_t pid){
 uint32_t p=dc_goal_new_basic(&f->r,type,240);if(!p)return 0;
 put(f,p,36,pid);put(f,p,68,128);put(f,p,76,p+108);put(f,p,80,p+236);put(f,p,84,p+108);return p;
}
static int setup(fixture *f){
 dc_kernel_types_result types;uint32_t t,ct;memset(f,0,sizeof *f);f->hooks.current_owner=owner;
 f->memory=malloc(1048576);if(!f->memory||!dc_goal_init(&f->r,f->memory,1048576,0x01000000)||
 !dc_goal_install_kernel_types(&f->r,&types))return 0;
 t=dc_goal_symbol_value(&f->r,f->r.s7+DC_FIX_SYM_PROCESS_TYPE);
 f->a=process(f,t,1);f->b=process(f,t,2);f->c=process(f,t,3);
 ct=dc_goal_define_type(&f->r,"kernel-context",f->r.basic_type,48,0,9);
 f->context=dc_goal_new_basic(&f->r,ct,48);f->symbol=dc_goal_intern(&f->r,"*kernel-context*");
 if(!f->a||!f->b||!f->c||!f->context||!dc_goal_set_symbol(&f->r,f->symbol,f->context))return 0;
 put(f,f->context,20,f->a);return dc_goal_process_heap_open(&f->allocation,&f->r,f->a,240);
}
static int enter(fixture *f,uint32_t p){return dc_goal_pp_scope_enter(&f->stack,&f->r,f->context,f->a,240,p,240,&f->allocation,&f->allocation);}
static int leave(fixture *f,uint32_t p){return dc_goal_pp_scope_leave(&f->stack,&f->r,p,240,&f->allocation);}
static int resolve(fixture *f,uint32_t *p){return dc_goal_pp_scope_resolve(&f->r,f->context,f->a,240,&f->allocation,p);}
static void nested(fixture *f){
 uint32_t out=0,basea=word(f,f->a,84),baseb=word(f,f->b,84),basec=word(f,f->c,84);
 unsigned char before[48];memcpy(before,dc_goal_resolve(&f->r,f->context-4,48),48);
 CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_ABSENT&&out==0);
 CHECK(dc_goal_pp_scope_init(&f->stack,&f->r,&f->hooks));
 CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_ABSENT);
 CHECK(enter(f,f->b));CHECK(f->allocation.process==f->b);CHECK(resolve(f,&out)==1&&out==f->b);
 CHECK(dc_goal_process_alloc(&f->allocation,1)==baseb);CHECK(word(f,f->a,84)==basea);CHECK(word(f,f->b,84)==baseb+16);
 CHECK(enter(f,f->c));CHECK(resolve(f,&out)==1&&out==f->c);
 CHECK(dc_goal_process_alloc(&f->allocation,17)==basec);CHECK(word(f,f->c,84)==basec+32);
 CHECK(!leave(f,f->b));CHECK(dc_goal_pp_scope_depth(&f->r)==2&&f->allocation.process==f->c);
 f->r.error=DC_GOAL_DIVIDE_BY_ZERO;CHECK(resolve(f,&out)==1&&out==f->c);CHECK(f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(leave(f,f->c));CHECK(f->allocation.process==f->b&&f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(resolve(f,&out)==1&&out==f->b&&f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(leave(f,f->b));CHECK(f->allocation.process==f->a&&f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(!dc_goal_pp_scope_blocks_transfer(&f->r)&&dc_goal_pp_scope_runtime_clear_allowed(&f->r));
 CHECK(memcmp(before,dc_goal_resolve(&f->r,f->context-4,48),48)==0);
 f->r.error=DC_GOAL_OK;CHECK(dc_goal_process_alloc(&f->allocation,1)==basea);
 CHECK(word(f,f->b,84)==baseb+16&&word(f,f->c,84)==basec+32);
 CHECK(dc_goal_pp_scope_detach(&f->stack,&f->r));
}
static void stale(fixture *f){
 uint32_t out=0,old;unsigned i;dc_goal_pp_scope_stack saved;dc_goal_process_heap allocation;
 unsigned char bytes[3][240],context[48];
 CHECK(dc_goal_pp_scope_init(&f->stack,&f->r,&f->hooks));CHECK(enter(f,f->b));CHECK(enter(f,f->c));
 for(i=0;i<6;++i){
  uint32_t p=i==0?f->a:i==1?f->b:i==2?f->c:i==3?f->c:f->context;
  unsigned offset=i<3?36:i==3?84:20;old=word(f,p,offset);
  if(i==5)dc_goal_set_symbol(&f->r,f->symbol,f->r.s7);else put(f,p,offset,i==4?f->b:old+1);
  memcpy(bytes[0],dc_goal_resolve(&f->r,f->a-4,240),240);memcpy(bytes[1],dc_goal_resolve(&f->r,f->b-4,240),240);
  memcpy(bytes[2],dc_goal_resolve(&f->r,f->c-4,240),240);memcpy(context,dc_goal_resolve(&f->r,f->context-4,48),48);
  saved=f->stack;allocation=f->allocation;out=123;
  CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED&&out==123);CHECK(!enter(f,f->b));
  CHECK(memcmp(&saved,&f->stack,sizeof saved)==0&&memcmp(&allocation,&f->allocation,sizeof allocation)==0);
  CHECK(memcmp(bytes[0],dc_goal_resolve(&f->r,f->a-4,240),240)==0);
  CHECK(memcmp(bytes[1],dc_goal_resolve(&f->r,f->b-4,240),240)==0);
  CHECK(memcmp(bytes[2],dc_goal_resolve(&f->r,f->c-4,240),240)==0);
  CHECK(memcmp(context,dc_goal_resolve(&f->r,f->context-4,48),48)==0);
  if(i==5)dc_goal_set_symbol(&f->r,f->symbol,f->context);else put(f,p,offset,old);f->r.error=DC_GOAL_OK;
  CHECK(resolve(f,&out)==1&&out==f->c);
 }
 f->r.error=DC_GOAL_DIVIDE_BY_ZERO;put(f,f->c,84,word(f,f->c,84)+1);
 CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED&&f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(leave(f,f->c)&&leave(f,f->b));CHECK(f->allocation.process==f->a&&f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(dc_goal_pp_scope_detach(&f->stack,&f->r));
}
static void metadata(fixture *f){
 unsigned i,depth;uint32_t out=5;dc_goal_process_heap allocation;dc_goal_pp_scope_stack snapshot;
 f->stack.hooks=f->hooks;CHECK(dc_goal_pp_scope_init(&f->stack,&f->r,&f->stack.hooks));
 CHECK(f->stack.hooks.current_owner==owner);CHECK(enter(f,f->b));
 {
  dc_goal_runtime foreign=f->r;dc_goal_runtime *saved_runtime=f->stack.runtime;
  foreign.memory=(unsigned char*)&f->stack;foreign.capacity=sizeof f->stack;
  CHECK(dc_goal_pp_scope_resolve(&foreign,0,0,0,&f->allocation,&out)==DC_GOAL_PP_SCOPE_ABSENT);
  CHECK(!dc_goal_pp_scope_blocks_transfer(&foreign)&&dc_goal_pp_scope_runtime_clear_allowed(&foreign));
  CHECK(!dc_goal_pp_scope_abort(&f->stack,&foreign));
  CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_READY&&out==f->b);
  f->stack.runtime=&foreign;f->r.error=DC_GOAL_OK;
  CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED);
  CHECK(dc_goal_pp_scope_blocks_transfer(&f->r)&&!dc_goal_pp_scope_runtime_clear_allowed(&f->r));
  f->stack.runtime=saved_runtime;f->r.error=DC_GOAL_OK;out=5;
 }
 owner_id=78;allocation=f->allocation;snapshot=f->stack;
 CHECK(!enter(f,f->c)&&!leave(f,f->b));CHECK(memcmp(&snapshot,&f->stack,sizeof snapshot)==0);
 CHECK(memcmp(&allocation,&f->allocation,sizeof allocation)==0);CHECK(!dc_goal_pp_scope_abort(&f->stack,&f->r));
 owner_id=77;f->r.error=DC_GOAL_OK;
 CHECK(!dc_goal_pp_scope_init(&f->stack,&f->r,&f->hooks));CHECK(dc_goal_pp_scope_depth(&f->r)==1);f->r.error=DC_GOAL_OK;
 for(i=1;i<DC_GOAL_PP_SCOPE_MAX_DEPTH;++i)CHECK(enter(f,f->b));
 snapshot=f->stack;allocation=f->allocation;CHECK(!enter(f,f->b));
 CHECK(memcmp(&snapshot,&f->stack,sizeof snapshot)==0&&memcmp(&allocation,&f->allocation,sizeof allocation)==0);
 depth=f->stack.depth;f->stack.depth=DC_GOAL_PP_SCOPE_MAX_DEPTH+1;f->r.error=DC_GOAL_OK;
 CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED&&out==5);CHECK(!leave(f,f->b));
 CHECK(dc_goal_pp_scope_blocks_transfer(&f->r)&&!dc_goal_pp_scope_runtime_clear_allowed(&f->r));
 CHECK(dc_goal_pp_scope_depth(&f->r)==0&&dc_goal_pp_scope_active(&f->r)==NULL);
 f->stack.depth=depth;f->r.error=DC_GOAL_OK;
 CHECK(!dc_goal_pp_scope_enter(&f->stack,&f->r,f->context,f->a,240,f->c,240,&f->allocation,&f->stack.frames[0].saved_allocation));
 for(i=0;i<depth;++i)CHECK(leave(f,f->b));
 CHECK(dc_goal_pp_scope_detach(&f->stack,&f->r));
}
static void fibers(fixture *f){
 dc_goal_fiber fiber,foreign;dc_goal_process_heap restored;uint32_t out=0;
 memset(&fiber,0,sizeof fiber);fiber.runtime=&f->r;fiber.phase=DC_FIBER_RUNNING;fiber.platform_owner=77;
 fiber.process=f->a;fiber.reservation=240;foreign=fiber;
 CHECK(dc_goal_pp_scope_init(&f->stack,&f->r,&f->hooks));fake_current=&fiber;
 CHECK(enter(f,f->b));CHECK(resolve(f,&out)==1&&out==f->b);CHECK(dc_goal_pp_scope_blocks_transfer(&f->r));
 fiber.phase=DC_FIBER_SUSPENDED;CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED);CHECK(!leave(f,f->b));
 fiber.phase=DC_FIBER_RUNNING;fake_current=&foreign;f->r.error=DC_GOAL_OK;
 CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED);CHECK(!leave(f,f->b));
 fake_current=NULL;f->r.error=DC_GOAL_OK;CHECK(resolve(f,&out)==DC_GOAL_PP_SCOPE_DENIED);CHECK(!leave(f,f->b));
 fake_current=&fiber;f->r.error=DC_GOAL_DIVIDE_BY_ZERO;
 CHECK(dc_goal_pp_scope_native_return(&f->r,&restored)==DC_GOAL_PP_SCOPE_READY);
 CHECK(restored.process==f->a&&restored.runtime==&f->r);CHECK(f->r.error==DC_GOAL_DIVIDE_BY_ZERO);
 CHECK(dc_goal_pp_scope_depth(&f->r)==0&&!dc_goal_pp_scope_blocks_transfer(&f->r));
 CHECK(word(f,f->context,20)==f->a);f->allocation=restored;f->r.error=DC_GOAL_OK;
 CHECK(enter(f,f->b));f->stack.depth=DC_GOAL_PP_SCOPE_MAX_DEPTH+1;
 CHECK(dc_goal_pp_scope_native_return(&f->r,&restored)==DC_GOAL_PP_SCOPE_DENIED);
 CHECK(dc_goal_pp_scope_active(&f->r)==NULL);CHECK(word(f,f->context,20)==f->a);
 fake_current=NULL;CHECK(dc_goal_pp_scope_abort(&f->stack,&f->r));
}
int main(int argc,char **argv){
 fixture f;int group=argc>1?atoi(argv[1]):0;
 if(!setup(&f))return 2;
 if(group==0)nested(&f);else if(group==1)stale(&f);else if(group==2)metadata(&f);else if(group==3)fibers(&f);else return 3;
 printf("pp scope %u/%u checks group=%d\n",passed,total,group);free(f.memory);return passed==total?0:1;
}
