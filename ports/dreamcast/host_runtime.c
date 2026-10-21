/* Native host runner for the shared runtime cases (AI-assisted), ISC. */
#include "runtime_cases.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
typedef struct { unsigned total,passed,failed; } report;
static void check(uint64_t actual,uint64_t expected,unsigned id,const char *name,void *context) {
  report *result=context;
  ++result->total;
  if (actual==expected) { ++result->passed; return; }
  if (!result->failed) result->failed=id;
  fprintf(stderr,"FAIL %u %s: observed=%llx expected=%llx\n",id,name,
          (unsigned long long)actual,(unsigned long long)expected);
}
int main(void) {
  const uint32_t capacity=1024u*1024u;
  void *allocation=malloc(capacity+64);
  unsigned char *memory;
  dc_runtime_stats stats;
  report result={0,0,0};
  if (!allocation) return 2;
  memory=(unsigned char*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u)+16;
  dc_runtime_cases(memory,capacity,0x8c100000u,check,&result,&stats);
  free(allocation);
  printf("{\"tests\":%u,\"passed\":%u,\"failed_case\":%u,\"arena_bytes\":%u,"
         "\"boot_bytes\":%u,\"live_before_stress\":%u,\"symbols\":%u}\n",
         result.total,result.passed,result.failed,stats.arena_bytes,stats.boot_bytes,
         stats.live_before_stress,stats.symbols);
  return result.failed?1:0;
}
