/* Execute unchanged original sort across an SH-4 literal island (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/sort_fixture.h"
#include "build/original_sort.h"
typedef uint64_t (*sort_function)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_sort_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                               unsigned *id,dc_goal_cache_sync sync) {
  static const uint32_t vectors[6][8]={{0},{9},{4,1},{5,1,4,2,3},{9,2,9,0,7,2,1,5},{3,1,2}};
  static const unsigned lengths[]={0,1,2,5,8,3};
  static const char *comparisons[]={"dc-sort-integer","dc-sort-boolean"};
  dc_goal_module_result callbacks,original;
  sort_function sort;
  uint32_t links[8],expected[8],head,tail,value,next,temporary,compare;
  unsigned mode,vector,i,j,n;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_sort_fixture_exports,GOAL_SORT_FIXTURE_EXPORT_COUNT,
        GOAL_SORT_FIXTURE_DATA,GOAL_SORT_FIXTURE_DATA_COUNT,sync,NULL,&callbacks),1,"sort comparison functions link");
  CHECK(dc_goal_load_module_data(rt,goal_original_sort_exports,GOAL_ORIGINAL_SORT_EXPORT_COUNT,
        GOAL_ORIGINAL_SORT_DATA,GOAL_ORIGINAL_SORT_DATA_COUNT,sync,NULL,&original),1,"original sort with literal island links");
  if (!callbacks.functions || !original.functions) return 0;
  CHECK(callbacks.functions,2,"integer and canonical boolean sort callbacks");
  CHECK(original.functions,1,"unchanged original kernel sort");
  sort=(sort_function)(uintptr_t)entry(rt,"sort");
  for (mode=0;mode<2;++mode) for (vector=0;vector<6;++vector) {
    n=lengths[vector]; tail=vector==5?rt->s7:rt->s7-10; head=tail;
    memcpy(expected,vectors[vector],n*sizeof(uint32_t));
    for (i=1;i<n;++i) {
      temporary=expected[i]; j=i;
      while (j && expected[j-1]>temporary) { expected[j]=expected[j-1]; --j; }
      expected[j]=temporary;
    }
    for (i=n;i>0;--i) {
      head=dc_goal_new_pair(rt,vectors[vector][i-1],head); links[i-1]=head;
      if (!head) { CHECK(0,1,"sort list allocation"); return callbacks.code_bytes+original.code_bytes; }
    }
    compare=entry(rt,comparisons[mode]);
    CHECK(sort(head,compare),head,"original sort preserves the list head");
    for (i=0;i<n;++i) {
      memcpy(&value,dc_goal_resolve(rt,links[i]-2,4),4);
      memcpy(&next,dc_goal_resolve(rt,links[i]+2,4),4);
      CHECK(value,expected[i],"original sort agrees with independent insertion ordering");
      CHECK(next,i+1<n?links[i+1]:tail,"original sort preserves all list links");
      head=next;
    }
    CHECK(head,tail,"original sort preserves proper or dotted terminator");
  }
  CHECK(rt->error,DC_GOAL_OK,"original sort callbacks and pool skips leave runtime error clear");
#undef CHECK
  return callbacks.code_bytes+original.code_bytes;
}
