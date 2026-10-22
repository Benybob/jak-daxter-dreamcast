/* Native structured images and original kernel-context form (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/structured_static_fixture.h"
#include "build/original_kernel_context.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word(dc_goal_runtime *rt,uint32_t object,unsigned offset) {
  uint32_t value; memcpy(&value,dc_goal_resolve(rt,object+offset,4),4); return value;
}
uint32_t dc_runtime_structured_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                    unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result fixture,original;
  uint32_t record_type,plain_type,root,child,plain,third,ctx,used,symbols,handler;
  uint64_t result=UINT64_MAX; one value,flags,get_child,serial,count,get_record; two call;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  used=rt->used; symbols=rt->symbol_count;
  CHECK(dc_goal_load_module_data(rt,goal_structured_static_fixture_exports,GOAL_STRUCTURED_STATIC_FIXTURE_EXPORT_COUNT,
        GOAL_STRUCTURED_STATIC_FIXTURE_DATA,GOAL_STRUCTURED_STATIC_FIXTURE_DATA_COUNT,sync,NULL,&fixture),0,
        "missing structured type rejects transactionally");
  CHECK(rt->error,DC_GOAL_BAD_MODULE,"missing structured type error explicit");
  CHECK(rt->used,used,"failed structured link rewinds data and code");
  CHECK(rt->symbol_count,symbols,"failed structured link restores namespace");
  CHECK(dc_goal_find(rt,"dc-structured-get"),0,"failed structured link publishes no functions");
  CHECK(dc_goal_find(rt,"dc-structured-root"),0,"failed structured link publishes no global cells");
  record_type=dc_goal_define_type(rt,"dc-structured-record",rt->basic_type,32,0,9);
  plain_type=dc_goal_define_type(rt,"dc-structured-plain",rt->structure_type,8,0,9);
  CHECK(record_type && plain_type,1,"structured runtime schemas match original declarations");
  CHECK(dc_goal_load_module_data(rt,goal_structured_static_fixture_exports,GOAL_STRUCTURED_STATIC_FIXTURE_EXPORT_COUNT,
        GOAL_STRUCTURED_STATIC_FIXTURE_DATA,GOAL_STRUCTURED_STATIC_FIXTURE_DATA_COUNT,sync,NULL,&fixture),1,
        "structured native graph links with type/function/symbol/data relocations");
  if(!fixture.functions) return 0;
  CHECK(fixture.functions,13,"structured exports include static lambdas and explicit initializer");
  CHECK(fixture.data_objects,4,"three basic images and one plain structure");
  CHECK(fixture.data_bytes,104,"structured source byte payloads retained");
  CHECK(entry(rt,"dc-structured-root"),0,"structured linking does not run initialization");
  CHECK(dc_goal_run_initializer(rt,GOAL_STRUCTURED_STATIC_FIXTURE_INITIALIZER,&result),1,"structured explicit initialization");
  CHECK(result,rt->s7,"structured initializer preserves original final false");
  root=(uint32_t)((zero)(uintptr_t)entry(rt,"dc-structured-get"))();
  plain=(uint32_t)((zero)(uintptr_t)entry(rt,"dc-structured-get-plain"))();
  value=(one)(uintptr_t)entry(rt,"dc-structured-value"); flags=(one)(uintptr_t)entry(rt,"dc-structured-flags");
  get_child=(one)(uintptr_t)entry(rt,"dc-structured-child"); serial=(one)(uintptr_t)entry(rt,"dc-structured-serial");
  call=(two)(uintptr_t)entry(rt,"dc-structured-call"); count=(one)(uintptr_t)entry(rt,"dc-structured-count");
  get_record=(one)(uintptr_t)entry(rt,"dc-structured-get-record");
  child=(uint32_t)get_child(root); third=(uint32_t)get_record(plain);
  CHECK(dc_goal_type_of(rt,root),record_type,"structured root type relocated");
  CHECK(dc_goal_type_of(rt,child),record_type,"nested structured child type relocated");
  CHECK(dc_goal_type_of(rt,third),record_type,"plain structure data reference relocated");
  CHECK(value(root),UINT64_C(0x12345678abcdef01),"raw structured signed64 payload");
  CHECK(value(child),(uint64_t)-7,"negative structured payload");
  CHECK(value(third),UINT64_C(0x100000002),"plain nested high-half payload");
  CHECK(flags(root),rt->s7+8,"structured true symbol relocated");
  CHECK(flags(child),rt->s7,"structured false symbol relocated");
  CHECK(get_child(child),rt->s7,"terminal child false relocated");
  CHECK(serial(root),0x13579bdfu,"original typed unsigned field");
  CHECK(serial(child),99,"child unsigned field");
  CHECK(count(plain),42,"untagged structure field load");
  CHECK(plain&15u,0,"plain static structure alignment");
  CHECK(call(root,UINT64_C(0x100000001)),UINT64_C(0x10000000a),"root static function relocation executes");
  CHECK(call(child,UINT64_C(0x100000001)),UINT64_C(0x100000014),"child static lambda has distinct target");
  CHECK(call(third,UINT64_C(0x100000001)),UINT64_C(0x10000001e),"plain nested static lambda target");
  handler=entry(rt,"dc-structured-callback-root");
  CHECK(dc_goal_set_symbol(rt,dc_goal_find(rt,"dc-structured-callback-root"),entry(rt,"dc-structured-callback-child")),1,
        "function cell can change independently of static function field");
  CHECK(call(root,5),14,"static function reference remains original direct body");
  CHECK(dc_goal_set_symbol(rt,dc_goal_find(rt,"dc-structured-callback-root"),handler),1,"static callback cell restored");
  CHECK(dc_goal_load_module_data(rt,goal_original_kernel_context_exports,GOAL_ORIGINAL_KERNEL_CONTEXT_EXPORT_COUNT,
        GOAL_ORIGINAL_KERNEL_CONTEXT_DATA,GOAL_ORIGINAL_KERNEL_CONTEXT_DATA_COUNT,sync,NULL,&original),1,
        "original one-form kernel context slice links");
  if(!original.functions) return fixture.code_bytes;
  CHECK(original.functions,1,"original context has only explicit initializer");
  CHECK(original.data_objects,1,"one unchanged original static kernel object");
  CHECK(original.data_bytes,48,"original kernel context byte size");
  CHECK(entry(rt,"*kernel-context*"),0,"original context global not initialized by linking");
  used=rt->used;
  CHECK(dc_goal_run_initializer(rt,GOAL_ORIGINAL_KERNEL_CONTEXT_INITIALIZER,&result),1,"original kernel context initializer executes");
  ctx=entry(rt,"*kernel-context*");
  CHECK(result,ctx,"original define returns actual context reference");
  CHECK(dc_goal_type_of(rt,ctx),entry(rt,"kernel-context"),"original context runtime type relocated");
  CHECK(word(rt,ctx,0),65,"original prevent-from-run execute and sleep flags");
  CHECK(word(rt,ctx,4),0,"original require-for-run default");
  CHECK(word(rt,ctx,8),0,"original allow-to-run default");
  CHECK(word(rt,ctx,12),2,"original next-pid starts at two");
  CHECK(word(rt,ctx,16),0,"later scratchpad top assignment excluded");
  CHECK(word(rt,ctx,20),rt->s7,"original current-process false");
  CHECK(word(rt,ctx,24),rt->s7,"original relocating-process false");
  CHECK(word(rt,ctx,28),0,"original relocating-min default");
  CHECK(word(rt,ctx,32),0,"original relocating-max default");
  CHECK(word(rt,ctx,36),0,"original relocating-offset default");
  CHECK(word(rt,ctx,40),rt->s7+8,"original low-memory-message true");
  CHECK(rt->used,used,"context startup reuses linked image without dynamic allocations");
  CHECK(dc_goal_find(rt,"*dram-stack*"),0,"context range excludes later execution stack");
  CHECK(dc_goal_find(rt,"*fake-scratchpad-data*"),0,"context range excludes desktop scratchpad setup");
  CHECK(rt->error,DC_GOAL_OK,"structured images and original context leave runtime clear");
#undef CHECK
  return fixture.code_bytes+original.code_bytes;
}
