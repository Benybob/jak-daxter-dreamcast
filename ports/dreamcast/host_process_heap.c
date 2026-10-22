/* Host execution of the real bounded process allocator (AI-assisted), ISC. */
#include "process_heap_cases.h"
#include <stdio.h>
typedef struct { unsigned total,passed; } report;
static void check(uint64_t a,uint64_t b,unsigned id,const char *label,void *context) {
  report *r=context; ++r->total;
  if(a==b) ++r->passed;
  else fprintf(stderr,"FAIL %u %s: %llx != %llx\n",id,label,(unsigned long long)a,(unsigned long long)b);
}
int main(void) {
  report r={0,0}; unsigned id=0;
  dc_process_heap_cases(check,&r,&id);
  printf("{\"tests\":%u,\"passed\":%u}\n",r.total,r.passed);
  return r.total==r.passed?0:1;
}
