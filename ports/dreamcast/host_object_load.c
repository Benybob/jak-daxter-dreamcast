/* Host original-data loader acceptance runner (AI-assisted), ISC. */
#include "object_load_cases.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct { unsigned total,passed; } report;
static void check(uint64_t a,uint64_t b,unsigned id,const char *label,void *context) {
  report *r=context; ++r->total;
  if (a==b) ++r->passed;
  else fprintf(stderr,"FAIL %u %s: %llx != %llx\n",id,label,(unsigned long long)a,(unsigned long long)b);
}
int main(void) {
  void *allocation=malloc(1048608);
  void *memory=(void*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
  dc_goal_runtime rt; report r={0,0}; unsigned id=0;
  if (!allocation || !dc_goal_init(&rt,memory,1048576,0x8c100000u)) return 2;
  dc_object_load_cases(&rt,check,&r,&id);
  printf("{\"tests\":%u,\"passed\":%u}\n",r.total,r.passed);
  free(allocation); return r.total==r.passed?0:1;
}
