/* Declared GOAL enum ABI at native boundaries (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/enum_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*mixed)(float,uint64_t,float,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_enum_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                              unsigned *id,dc_goal_cache_sync sync) {
  static const uint64_t u32[]={0,1,UINT64_C(0x80000000),UINT64_C(0xffffffff)};
  static const uint64_t s32[]={0,1,(uint64_t)-1,(uint64_t)INT32_MIN,INT32_MAX};
  static const uint64_t u64[]={0,1,UINT64_C(0x100000000),UINT64_C(0x8000000000000000),UINT64_MAX,UINT64_C(0x8000000100000001)};
  static const uint64_t s64[]={0,1,(uint64_t)-1,(uint64_t)INT64_MIN,INT64_MAX};
  static const uint64_t u8[]={0,1,128,255},s8[]={0,1,(uint64_t)-1,(uint64_t)-128,127};
  static const uint64_t u16[]={0,1,32768,65535},s16[]={0,1,(uint64_t)-1,(uint64_t)-32768,32767};
  const uint64_t *values[]={u32,s32,u64,s64,u8,s8,u16,s16}; const unsigned counts[]={4,5,6,5,4,5,4,5};
  const char *const names[]={"u32","s32","u64","s64","u8","s8","u16","s16"};
  dc_goal_module_result module; one identities[8],loads[8]; two call,mixed_call; mixed mixing;
  uint32_t memory,i,j,k;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_enum_fixture_exports,GOAL_ENUM_FIXTURE_EXPORT_COUNT,sync,NULL,&module),1,
        "original frontend enum fixtures link");
  if(!module.functions) return 0;
  CHECK(module.functions,21,"twenty-one enum ABI functions");
  CHECK(sizeof(goal_dc_enum_u32),6,"unsigned enum32 compact return");
  CHECK(sizeof(goal_dc_enum_s32),6,"signed enum32 compact return");
  CHECK(sizeof(goal_dc_enum_u64),6,"unsigned enum64 compact return");
  CHECK(sizeof(goal_dc_enum_s64),6,"signed enum64 compact return");
  CHECK(sizeof(goal_dc_enum_u8),6,"unsigned enum8 compact return");
  CHECK(sizeof(goal_dc_enum_s8),6,"signed enum8 compact return");
  CHECK(sizeof(goal_dc_enum_u16),6,"unsigned enum16 compact return");
  CHECK(sizeof(goal_dc_enum_s16),6,"signed enum16 compact return");
  memory=dc_goal_alloc(rt,8);
  CHECK(memory!=0,1,"enum typed-memory allocation");
  if(!memory) return module.code_bytes;
  for(i=0;i<8;++i) {
    char identity[32]="dc-enum-",load[32]="dc-enum-load-";
    strcat(identity,names[i]); strcat(load,names[i]);
    identities[i]=(one)(uintptr_t)entry(rt,identity); loads[i]=(one)(uintptr_t)entry(rt,load);
    for(j=0;j<counts[i];++j) CHECK(identities[i](values[i][j]),values[i][j],"native enum value preserves declared signedness and bits");
  }
  CHECK(((zero)(uintptr_t)entry(rt,"dc-enum-bits32"))(),UINT64_C(0x80000001),"original unsigned enum32 high-bit construction");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-enum-bits64"))(),UINT64_C(0x8000000100000001),"original enum64 high-half construction");
  call=(two)(uintptr_t)entry(rt,"dc-enum-call");
  for(i=0;i<6;++i) for(j=0;j<5;++j)
    CHECK(call(u64[i],s32[j]),u64[i]^s32[j],"GOAL enum call uses native scalar ABI");
  mixing=(mixed)(uintptr_t)entry(rt,"dc-enum-mixed"); mixed_call=(two)(uintptr_t)entry(rt,"dc-enum-mixed-call");
  for(i=0;i<4;++i) for(j=0;j<6;++j) {
    CHECK(mixing(1.5f,u32[i],2.75f,u64[j]),u32[i]+u64[j]+3,"C mixed enum entry uses independent GPR and float streams");
    CHECK(mixed_call(u32[i],u64[j]),u32[i]+u64[j]+3,"GOAL mixed enum call uses independent register streams");
  }
  for(i=0;i<8;++i) for(k=0;k<counts[i];++k) {
    uint64_t value=values[i][k];
    memcpy(dc_goal_resolve(rt,memory,8),&value,8);
    CHECK(loads[i](memory),value,"typed enum memory load retains original width and extension");
  }
  CHECK(rt->error,DC_GOAL_OK,"enum ABI leaves runtime error clear");
#undef CHECK
  return module.code_bytes;
}
