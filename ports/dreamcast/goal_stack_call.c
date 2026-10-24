/* Validated synchronous alternate-stack call (AI-assisted), ISC. */
#include "goal_stack_call.h"
#ifdef __DREAMCAST__
#include "goal_register_snapshot.h"
#include "goal_kos_stack.h"
extern uint64_t dc_goal_stack_call_raw(uint32_t,uint32_t,uint32_t);
#endif
static unsigned stack_call_active;
static int bad(dc_goal_runtime *rt) {if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
 return a<=b?b-a<an:a-b<bn;
}
int dc_goal_call_on_stack(dc_goal_runtime *rt,uint32_t entry,uint32_t stack_base,
                          uint32_t stack_bytes,uint64_t *out) {
 dc_goal_code_entry_info code;
 void *memory;
 uintptr_t result_address,stack_address;
 if(!rt || !out || stack_call_active || stack_bytes<4096u ||
    (stack_base&15u) || (stack_bytes&15u) || stack_bytes>UINT32_MAX-stack_base) return bad(rt);
 if(!dc_goal_code_entry(rt,entry,&code)) return 0;
 memory=dc_goal_resolve(rt,stack_base,stack_bytes);
 if(!memory) return 0;
 if(overlap(stack_base,stack_bytes,code.entry,code.bytes)) return bad(rt);
 result_address=(uintptr_t)out;stack_address=(uintptr_t)memory;
#ifdef __DREAMCAST__
 result_address&=0x1fffffffu;stack_address&=0x1fffffffu;
#endif
 if(overlap(result_address,sizeof *out,stack_address,stack_bytes)) return bad(rt);
#ifdef __DREAMCAST__
 {
  struct dc_goal_native_regs caller;
  dc_goal_kos_stack os_stack;
  uint64_t value;
  if((rt->base&0xe0000000u) || !dc_goal_native_regs_snapshot(&caller)) return bad(rt);
  if(overlap(caller.sp&0x1fffffffu,1,stack_base,stack_bytes)) return bad(rt);
  stack_call_active=1;rt->error=DC_GOAL_OK;
  if(!dc_goal_kos_stack_enter(&os_stack,stack_base,stack_bytes)){stack_call_active=0;return bad(rt);}
  value=dc_goal_stack_call_raw(entry,stack_base+stack_bytes,os_stack.root_sr);
  stack_call_active=0;
  if(!dc_goal_kos_stack_leave(&os_stack))return bad(rt);
  if(rt->error!=DC_GOAL_OK) return 0;
  *out=value;return 1;
 }
#else
 /* Host validates provenance/bounds but never executes SH4 bytes. */
 return bad(rt);
#endif
}
