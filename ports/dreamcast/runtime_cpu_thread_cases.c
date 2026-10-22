/* Bounded data-only original CPU-thread construction (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/original_cpu_thread.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word(dc_goal_runtime *rt,uint32_t object,unsigned offset) {
  uint32_t v; memcpy(&v,dc_goal_resolve(rt,object+offset,4),4); return v;
}
static void set_word(dc_goal_runtime *rt,uint32_t object,unsigned offset,uint32_t value) {
  memcpy(dc_goal_resolve(rt,object+offset,4),&value,4);
}
uint32_t dc_runtime_cpu_thread_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                    unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result module;
  uint32_t type,thread_type=entry(rt,"thread"),process_type=entry(rt,"process"),name=dc_goal_intern(rt,"dc-cpu-owner");
  uint32_t owner,stack,raw,constructed,ctor_symbol,ctor,old_object,old_process,used,expected_cur,i,j;
  const uint32_t backups[]={0,64,256};
  unsigned char expected[624],stack_before[1024],zero[344]={0};
  two new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
  one asize;
  dc_goal_type_fields *schema;
  struct bad_case { uint32_t name,base,bytes,backup,reservation; enum dc_goal_error error; };
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_original_cpu_thread_exports,GOAL_ORIGINAL_CPU_THREAD_EXPORT_COUNT,
                           sync,NULL,&module),1,"unchanged original CPU-thread constructor and asize-of link");
  if(!module.functions) return 0;
  CHECK(module.functions,2,"two unchanged original CPU-thread data routines");
  type=dc_goal_define_type(rt,"cpu-thread",thread_type,128,0,12);
  CHECK(type!=0,1,"bounded CPU-thread schema matches original128-byte/12-method flags");
  if(!type) return module.code_bytes;
  schema=dc_goal_resolve(rt,type,sizeof(*schema));
  ctor_symbol=dc_goal_find(rt,"(method new cpu-thread)"); ctor=entry(rt,"(method new cpu-thread)");
  asize=(one)(uintptr_t)entry(rt,"(method asize-of cpu-thread)");
  old_object=dc_goal_get_method(rt,rt->object_type,0); old_process=dc_goal_get_method(rt,process_type,0);
  CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"sized allocation selected for original process owner");
  CHECK(dc_goal_set_method(rt,process_type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
  dc_goal_clear_process_context(rt);
  stack=dc_goal_alloc(rt,1024);
  CHECK(stack!=0,1,"caller owns disjoint aligned execution-stack reservation");
  if(!stack) return module.code_bytes;
  memset(dc_goal_resolve(rt,stack,1024),0xa5,1024); memset(stack_before,0xa5,1024);
  owner=(uint32_t)new_process(name,512);
  CHECK(owner!=0,1,"original constructor creates known512-byte process heap");
  if(!owner) return module.code_bytes;
  memcpy(expected,dc_goal_resolve(rt,owner-4,624),624);
  used=rt->used;
  {
    struct bad_case bad[]={
      {rt->string_type,stack,1024,64,624,DC_GOAL_BAD_TYPE},
      {name,stack+1,1024,64,624,DC_GOAL_BAD_ARGUMENT},
      {name,stack,0,0,624,DC_GOAL_BAD_ARGUMENT},
      {name,stack,1023,64,624,DC_GOAL_BAD_ARGUMENT},
      {name,stack,UINT32_MAX,64,624,DC_GOAL_BAD_ARGUMENT},
      {name,stack,1024,1,624,DC_GOAL_BAD_ARGUMENT},
      {name,stack,1024,1040,624,DC_GOAL_BAD_ARGUMENT},
      {name,stack,1024,384,624,DC_GOAL_OUT_OF_MEMORY},
      {name,stack,1024,400,624,DC_GOAL_OUT_OF_MEMORY},
      {name,stack,1024,64,623,DC_GOAL_BAD_ARGUMENT},
      {name,owner-4,16,0,624,DC_GOAL_BAD_ARGUMENT},
      {name,rt->base+rt->capacity-16,16,0,624,DC_GOAL_BOUNDS},
      {name,0xfffffff0u,32,0,624,DC_GOAL_BAD_ARGUMENT}
    };
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
      CHECK(dc_goal_construct_main_thread(rt,owner,bad[i].reservation,bad[i].base,bad[i].bytes,bad[i].name,bad[i].backup),0,
            "CPU-thread preflight rejects invalid or unsupported argument combination");
      CHECK(rt->error,bad[i].error,"CPU-thread preflight exposes exact error category");
      CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,624),624),0,"rejected constructor leaves all process/header/heap bytes unchanged");
      CHECK(memcmp(stack_before,dc_goal_resolve(rt,stack,1024),1024),0,"rejected constructor leaves execution stack unchanged");
    }
  }
  for(i=0;i<2;++i) {
    set_word(rt,owner,40+i*4,owner);
    CHECK(dc_goal_construct_main_thread(rt,owner,624,stack,1024,name,64),0,"main or top thread already present rejects unsupported reentry/temp thread");
    CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"existing-thread rejection explicit");
    set_word(rt,owner,40+i*4,rt->s7);
  }
  schema->allocated_size=127;
  CHECK(dc_goal_construct_main_thread(rt,owner,624,stack,1024,name,64),0,"mismatched CPU-thread schema rejects before original body");
  CHECK(rt->error,DC_GOAL_BAD_TYPE,"mismatched CPU-thread type error"); schema->allocated_size=128;
  CHECK(dc_goal_set_symbol(rt,ctor_symbol,0),1,"original constructor binding absent for strict preflight probe");
  CHECK(dc_goal_construct_main_thread(rt,owner,624,stack,1024,name,64),0,"missing original constructor refuses to allocate/write");
  CHECK(rt->error,DC_GOAL_BAD_MODULE,"missing original constructor explicit error");
  CHECK(dc_goal_set_symbol(rt,ctor_symbol,ctor),1,"unchanged original constructor binding restored");
  CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,624),624),0,"all additional rejection paths preserve full process reservation");
  CHECK(rt->used,used,"all constructor preflight failures allocate nothing");
  for(i=0;i<3;++i) {
    owner=(uint32_t)new_process(name,512);
    CHECK(owner!=0,1,"fresh original process for independent first-thread construction");
    if(!owner) return module.code_bytes;
    raw=word(rt,owner,84);
    memcpy(expected,dc_goal_resolve(rt,owner-4,624),624);
    constructed=dc_goal_construct_main_thread(rt,owner,624,stack,1024,name,backups[i]);
    CHECK(constructed,raw+4,"original main CPU-thread constructed at owned process heap cursor");
    if(!constructed) return module.code_bytes;
    CHECK(dc_goal_type_of(rt,constructed),type,"original constructor writes correct CPU-thread type header");
    CHECK(word(rt,constructed,0),name,"original CPU-thread name");
    CHECK(word(rt,constructed,4),owner,"original CPU-thread parent process");
    CHECK(word(rt,constructed,8),rt->s7,"original first-thread previous is false");
    CHECK(word(rt,constructed,12),dc_goal_get_method(rt,type,10),"original suspend hook equals declared method slot10");
    CHECK(word(rt,constructed,16),dc_goal_get_method(rt,type,11),"original resume hook equals declared method slot11");
    CHECK(word(rt,constructed,20),0,"constructor does not initialize a runnable PC");
    CHECK(word(rt,constructed,24),stack+1024,"original initial SP is supplied execution stack top");
    CHECK(word(rt,constructed,28),stack+1024,"original execution stack top");
    CHECK(word(rt,constructed,32),backups[i],"original backup stack byte capacity");
    CHECK(memcmp(zero,dc_goal_resolve(rt,constructed+36,88+backups[i]),88+backups[i]),0,"saved-register and backup area retain fresh process zeros");
    CHECK(asize(constructed),128+backups[i],"original CPU-thread asize-of matches header plus backup capacity");
    expected_cur=raw+128+backups[i];
    memcpy(expected+4+84,&expected_cur,4); memcpy(expected+4+44,&constructed,4);
    memcpy(expected+(raw-(owner-4)),dc_goal_resolve(rt,raw,128+backups[i]),128+backups[i]);
    CHECK(memcmp(expected,dc_goal_resolve(rt,owner-4,624),624),0,"only original thread payload, heap cursor and top-thread change");
    CHECK(word(rt,owner,40),rt->s7,"construction does not activate/install process main-thread");
    CHECK(word(rt,owner,32),dc_goal_find(rt,"dead"),"constructor leaves original process status dead");
    CHECK(memcmp(stack_before,dc_goal_resolve(rt,stack,1024),1024),0,"data construction never executes on or writes execution stack");
    for(j=0;j<3;++j) CHECK(word(rt,constructed,36+j*8),0,"known saved-register lows stay zero before explicit context initialization");
    CHECK(dc_goal_construct_main_thread(rt,owner,624,stack,1024,name,backups[i]),0,"second construction cannot silently use temporary-thread branch");
    CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"repeated construction explicit unsupported-thread error");
  }
  CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object constructor binding restored");
  CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor binding restored");
  rt->error=DC_GOAL_OK;
#undef CHECK
  return module.code_bytes;
}
