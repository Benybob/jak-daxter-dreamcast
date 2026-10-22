/* Independent boundary table for actual production predicates (AI-assisted), ISC. */
#ifndef DC_FIBER_BOUNDS_CASES_H
#define DC_FIBER_BOUNDS_CASES_H
#include "fiber_bounds.h"
#include "runtime_cases.h"
static void dc_fiber_bounds_cases(dc_runtime_check check,void *context,unsigned *id){
 const uint32_t sp[]={0x1000,0x1001,0x1003,0x1004,0x1ffc,0x2000,0x2001,0x2004,0x0ffc,0};
 const unsigned leave[]={0,0,0,1,1,1,0,0,0,0};
 const unsigned root[]={0,0,0,0,0,0,0,1,1,0};
 unsigned i;
 for(i=0;i<10;++i){
  check(dc_fiber_leave_push_ok(0x1000,0x1000,sp[i]),leave[i],++*id,"live fiber word-push boundary/alignment",context);
  check(dc_fiber_root_push_ok(0x1000,0x1000,sp[i]),root[i],++*id,"root marker and whole word-push disjointness",context);
 }
 check(dc_fiber_leave_push_ok(0xfffffff0u,32,4),0,++*id,"leave span overflow rejects",context);
 check(dc_fiber_root_push_ok(0xfffffff0u,32,4),0,++*id,"root span overflow rejects",context);
 check(dc_fiber_leave_push_ok(0x1000,0,0x1000),0,++*id,"empty leave span rejects",context);
 check(dc_fiber_root_push_ok(0x1000,0,0x1000),0,++*id,"empty root span rejects",context);
}
#endif
