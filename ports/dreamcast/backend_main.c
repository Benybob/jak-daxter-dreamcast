/* Native SH-4 codegen/ABI/cache bootstrap, AI-assisted. Not a GOAL kernel. */
#include <kos.h>
#include <arch/cache.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "build/backend_cases.h"
#ifdef DC_GOAL_FRONTEND
#include "build/goal_fixture.h"
#endif
#ifdef DC_GAME_LEAF
#include "build/game_leaf.h"
#endif

KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {
  uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
  uint32_t observed_lo,observed_hi,expected_lo,expected_hi;
  char description[80];
} backend_telemetry;
typedef char backend_layout_check[sizeof(backend_telemetry)==128?1:-1];
volatile backend_telemetry dc_backend={.magic=0x34485347u,.version=1};
static uint32_t global_value=0x76543210u;
static int32_t bridge(int32_t value) { return value*3+1; }

static void check(uint64_t actual,uint64_t expected,unsigned which,const char *name) {
  unsigned i;
  ++dc_backend.total;
  if (actual==expected) { ++dc_backend.passed; return; }
  if (dc_backend.failed_case) return;
  dc_backend.failed_case=which;
  dc_backend.observed_lo=(uint32_t)actual; dc_backend.observed_hi=(uint32_t)(actual>>32);
  dc_backend.expected_lo=(uint32_t)expected; dc_backend.expected_hi=(uint32_t)(expected>>32);
  for (i=0;i<sizeof(dc_backend.description)-1 && name[i];++i) dc_backend.description[i]=name[i];
  dc_backend.description[i]=0;
}

static void check_float(float actual,float expected,unsigned which,const char *name) {
  uint32_t a,b;
  memcpy(&a,&actual,4); memcpy(&b,&expected,4);
  check(a,b,which,name);
}

static void sync_code(void *code,size_t bytes) {
  dcache_flush_range((uintptr_t)code,bytes);
  icache_flush_range((uintptr_t)code,bytes);
}
static void *load(const unsigned char *data,size_t bytes) {
  void *memory=memalign(32,(bytes+31)&~31u);
  if (!memory) return NULL;
  memcpy(memory,data,bytes);
  dc_backend.code_bytes+=(uint32_t)bytes;
  sync_code(memory,bytes);
  return memory;
}

int main(void) {
  void *code[11]={0};
  unsigned i;
  uint32_t number=0;
  typedef int32_t (*two32)(int32_t,int32_t);
  typedef int32_t (*one32)(int32_t);
  typedef uint64_t (*two64)(uint64_t,uint64_t);
  typedef float (*twofloat)(float,float);
  typedef uint32_t (*memory32)(uint32_t*,uint32_t);
  typedef uint32_t (*zero32)(void);
  dc_backend.state=1;
  dc_backend.system_ram=_arch_mem_top-0x8c000000u;
#define LOAD(i,name) code[i]=load(probe_##name,sizeof(probe_##name))
  LOAD(0,add32); LOAD(1,choose32); LOAD(2,factorial32); LOAD(3,literal32);
  LOAD(4,add64); LOAD(5,sub64); LOAD(6,addfloat); LOAD(7,memory32);
  LOAD(8,call_bridge); LOAD(9,global_load); LOAD(10,cache_patch);
#undef LOAD
  for (i=0;i<11;++i) if (!code[i]) {
    dc_backend.failed_case=100+i; dc_backend.state=3;
    for (i=0;i<11;++i) free(code[i]);
    return 1;
  }
  { uint32_t value=(uint32_t)(uintptr_t)bridge;
    memcpy((char*)code[8]+PROBE_CALL_BRIDGE_BRIDGE_OFFSET,&value,4);
    sync_code(code[8],sizeof(probe_call_bridge)); }
  { uint32_t value=(uint32_t)(uintptr_t)&global_value;
    memcpy((char*)code[9]+PROBE_GLOBAL_LOAD_GLOBAL_VALUE_OFFSET,&value,4);
    sync_code(code[9],sizeof(probe_global_load)); }
  check((uint32_t)((two32)code[0])(20,22),42,1,"add32 positive");
  check((uint32_t)((two32)code[0])(-3,1),(uint32_t)-2,2,"add32 negative");
  check((uint32_t)((one32)code[1])(-9),9,3,"signed branch negative");
  check((uint32_t)((one32)code[1])(9),14,4,"signed branch positive");
  check((uint32_t)((one32)code[2])(0),1,5,"factorial zero");
  check((uint32_t)((one32)code[2])(6),720,6,"backward branch factorial");
  check(((zero32)code[3])(),0x12345678u,7,"PC-relative literal pool");
  check(((two64)code[4])(0xffffffffu,1),0x100000000ull,8,"add64 carry");
  check(((two64)code[4])(0x7fffffffffffffffull,1),0x8000000000000000ull,9,"add64 sign boundary");
  check(((two64)code[5])(0x100000000ull,1),0xffffffffu,10,"sub64 borrow");
  check_float(((twofloat)code[6])(1.5f,2.25f),3.75f,11,"single-precision addition");
  check(((memory32)code[7])(&number,99),99,12,"load/store return");
  check(number,99,13,"load/store memory");
  check((uint32_t)((one32)code[8])(7),25,14,"native C bridge/PR save");
  check(((zero32)code[9])(),global_value,15,"absolute data relocation");
  check(((zero32)code[10])(),42,16,"initial code cache");
  { uint16_t instruction=0xe02b; memcpy(code[10],&instruction,2);
    sync_code(code[10],sizeof(probe_cache_patch)); }
  check(((zero32)code[10])(),43,17,"modified code cache flush");
#ifdef DC_GOAL_FRONTEND
  {
    void *goal[7]={0};
    typedef uint64_t (*one64)(uint64_t);
#define LOAD_GOAL(i,name) goal[i]=load(goal_dc_goal_##name,sizeof(goal_dc_goal_##name))
    LOAD_GOAL(0,add); LOAD_GOAL(1,sub); LOAD_GOAL(2,choose);
    LOAD_GOAL(3,factorial); LOAD_GOAL(4,float);
    LOAD_GOAL(5,call);
    LOAD_GOAL(6,float_call);
#undef LOAD_GOAL
    for (i=0;i<7;++i) if (!goal[i]) {
      dc_backend.failed_case=200+i; dc_backend.state=3;
      for (i=0;i<7;++i) free(goal[i]);
      for (i=0;i<11;++i) free(code[i]);
      return 1;
    }
    { uint32_t value=(uint32_t)(uintptr_t)goal[0];
      memcpy((char*)goal[5]+GOAL_DC_GOAL_CALL_DC_GOAL_ADD_OFFSET,&value,4);
      value=(uint32_t)(uintptr_t)goal[1];
      memcpy((char*)goal[5]+GOAL_DC_GOAL_CALL_DC_GOAL_SUB_OFFSET,&value,4);
      sync_code(goal[5],sizeof(goal_dc_goal_call)); }
    { uint32_t value=(uint32_t)(uintptr_t)goal[4];
      memcpy((char*)goal[6]+GOAL_DC_GOAL_FLOAT_CALL_DC_GOAL_FLOAT_OFFSET,&value,4);
      sync_code(goal[6],sizeof(goal_dc_goal_float_call)); }
    check(((two64)goal[0])(20,22),42,18,"GOAL add");
    check(((two64)goal[0])(0xffffffffu,1),0x100000000ull,19,"GOAL add carry");
    check(((two64)goal[0])(0x7fffffffffffffffull,1),0x8000000000000000ull,20,"GOAL add sign boundary");
    check(((two64)goal[1])(0x100000000ull,1),0xffffffffu,21,"GOAL subtraction borrow");
    check(((one64)goal[2])((uint64_t)-9),9,22,"GOAL signed branch negative");
    check(((one64)goal[2])(9),14,23,"GOAL signed branch positive");
    check(((one64)goal[3])(0),1,24,"GOAL factorial zero");
    check(((one64)goal[3])(6),720,25,"GOAL factorial loop");
    check_float(((twofloat)goal[4])(1.5f,2.25f),-0.75f,26,"GOAL ordered float ABI/subtract");
    check(((two64)goal[0])((uint64_t)-3,1),(uint64_t)-2,27,"GOAL add negative");
    check(((two64)goal[5])(20,22),40,34,"Linked GOAL calls / live result");
    check(((two64)goal[5])((uint64_t)-3,1),(uint64_t)-6,35,"Linked GOAL calls negative");
    check(((two64)goal[5])(0x100000000ull,1),0x200000000ull,36,"Linked GOAL calls carry");
    check(((two64)goal[5])(0x7fffffffffffffffull,1),0xfffffffffffffffeull,37,"Linked GOAL calls overflow");
    check_float(((twofloat)goal[6])(1.5f,2.25f),-1.5f,38,"Linked GOAL float calls");
    check_float(((twofloat)goal[6])(-3.0f,1.0f),-8.0f,39,"Linked GOAL float calls negative");
    for (i=0;i<7;++i) free(goal[i]);
  }
#endif
#ifdef DC_GAME_LEAF
  {
    typedef float (*threefloat)(float,float,float);
    void *original=load(goal_lerp,sizeof(goal_lerp));
    if (!original) {
      dc_backend.failed_case=300; dc_backend.state=3;
      for (i=0;i<11;++i) free(code[i]);
      return 1;
    }
    check_float(((threefloat)original)(-10.0f,10.0f,0.0f),-10.0f,28,"Original lerp minimum");
    check_float(((threefloat)original)(-10.0f,10.0f,1.0f),10.0f,29,"Original lerp maximum");
    check_float(((threefloat)original)(-10.0f,10.0f,0.5f),0.0f,30,"Original lerp midpoint");
    check_float(((threefloat)original)(20.0f,24.0f,0.25f),21.0f,31,"Original lerp quarter");
    check_float(((threefloat)original)(2.0f,4.0f,-0.5f),1.0f,32,"Original lerp below interval");
    check_float(((threefloat)original)(2.0f,4.0f,1.5f),5.0f,33,"Original lerp above interval");
    free(original);
  }
#endif
  for (i=0;i<11;++i) free(code[i]);
  dc_backend.state=dc_backend.failed_case?3:2;
  printf("SH4_BACKEND %lu/%lu tests, emitted=%lu bytes, first_failure=%lu\n",
         (unsigned long)dc_backend.passed,(unsigned long)dc_backend.total,
         (unsigned long)dc_backend.code_bytes,(unsigned long)dc_backend.failed_case);
  thd_sleep(500);
  return dc_backend.failed_case?1:0;
}
