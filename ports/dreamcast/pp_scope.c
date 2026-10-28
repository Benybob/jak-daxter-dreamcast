/* Lexical pp metadata. Scheduler identity stays in the kernel context.
 * Allocation identity is an explicit record the bridge saves and restores.
 * (AI-assisted), ISC. */
#include "pp_scope.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

enum {
  PP_SCOPE_MAGIC = 0x50505343u,
  PP_SCOPE_ABORTED = 0x50504142u,
  PP_CONTEXT_BYTES = 48,
  PP_CONTEXT_VIEW = 44,
  PP_CURRENT_OFFSET = 20,
  PP_PID_OFFSET = 36
};

static dc_goal_pp_scope_stack *active_stack;
/* Registration ownership is not inferred from mutable caller metadata. */
static dc_goal_runtime *registration_runtime;

static uintptr_t address(const void *p) {
  uintptr_t value = (uintptr_t)p;
#ifdef __DREAMCAST__
  value &= 0x1fffffffu;
#endif
  return value;
}

static int overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn) {
  return an && bn && (a <= b ? b - a < an : a - b < bn);
}

static int storage_ok(const dc_goal_runtime *rt, const void *p, size_t bytes) {
  return rt && rt->memory && p &&
         !overlap(address(p), bytes, address(rt->memory), rt->capacity) &&
         !overlap(address(p), bytes, address(rt), sizeof *rt);
}

static int outside_fiber(const void *p, size_t bytes) {
  dc_goal_fiber *fiber = dc_goal_fiber_current();
  if (!fiber) return 1;
  return !overlap(address(p), bytes, address(fiber), sizeof *fiber);
}

static int c_storage(const dc_goal_runtime *rt, const dc_goal_pp_scope_stack *stack,
                     const void *p, size_t bytes) {
  return storage_ok(rt, p, bytes) && outside_fiber(p, bytes) &&
         !overlap(address(p), bytes, address(stack), sizeof *stack);
}

/* Identical pointers may alias. A partial overlap would tear the saved record. */
static int same_or_separate(const void *a, const void *b, size_t bytes) {
  if (a == b) return 1;
  return !overlap(address(a), bytes, address(b), bytes);
}

static int fail(dc_goal_runtime *rt, enum dc_goal_error prior) {
  if (!rt) return 0;
  if (prior != DC_GOAL_OK) rt->error = prior;
  else if (rt->error == DC_GOAL_OK) rt->error = DC_GOAL_BAD_ARGUMENT;
  return 0;
}

static int deny(dc_goal_runtime *rt, enum dc_goal_error prior) {
  fail(rt, prior);
  return DC_GOAL_PP_SCOPE_DENIED;
}

static uint32_t owner_now(const dc_goal_pp_scope_stack *stack) {
  if (!stack->hooks.current_owner) return 0;
  return stack->hooks.current_owner();
}

/* Reads phase, runtime, and platform_owner. Does not read SP, SR, or regs.
 * *seen is the one current-fiber sample for this call. */
static int anchor_ok(const dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                     int establishing, dc_goal_fiber **seen) {
  dc_goal_fiber *fiber = dc_goal_fiber_current();
  if (seen) *seen = fiber;
  if (owner_now(stack) != stack->owner) return 0;
  if (!fiber) return establishing || stack->anchor_direct;
  if (fiber->phase != DC_FIBER_RUNNING || fiber->runtime != rt) return 0;
  if (!stack->hooks.current_owner || !fiber->platform_owner || fiber->platform_owner != stack->owner)
    return 0;
  if (!establishing && (stack->anchor_direct || stack->fiber != fiber)) return 0;
  return 1;
}

static int context_current(dc_goal_runtime *rt, uint32_t context, uint32_t *current) {
  uint32_t symbol, type;
  dc_goal_type_fields *schema;
  unsigned char *fields;
  symbol = dc_goal_find(rt, "kernel-context");
  type = symbol ? dc_goal_symbol_value(rt, symbol) : 0;
  if (context < 4 || !type || dc_goal_type_of(rt, type) != rt->type_type ||
      dc_goal_type_of(rt, context) != type) return 0;
  schema = dc_goal_resolve(rt, type, sizeof *schema);
  if (!schema || schema->allocated_size != PP_CONTEXT_BYTES ||
      !dc_goal_resolve(rt, context - 4, PP_CONTEXT_BYTES)) return 0;
  fields = dc_goal_resolve(rt, context, PP_CONTEXT_VIEW);
  if (!fields) return 0;
  memcpy(current, fields + PP_CURRENT_OFFSET, 4);
  return 1;
}

static int scheduler_word(dc_goal_runtime *rt, uint32_t context, uint32_t scheduler) {
  uint32_t symbol, current = 0;
  symbol = dc_goal_find(rt, "*kernel-context*");
  if (!symbol || dc_goal_symbol_value(rt, symbol) != context) return 0;
  if (!context_current(rt, context, &current) || current != scheduler) return 0;
  return 1;
}

static int open_reservation(dc_goal_runtime *rt, uint32_t process, uint32_t bytes,
                            dc_goal_process_heap *out) {
  if (!dc_goal_process_heap_open(out, rt, process, bytes)) return 0;
  return out->runtime == rt && out->process == process && out->allocation_bytes == bytes;
}

/* The root scheduler: C top level, where desktop call_goal runs with pp = #f
 * (r13 = the symbol table). It owns no process reservation, so its saved
 * allocation is the empty record. Only the root C stack establishes it. */
static int root_scheduler(const dc_goal_runtime *rt, uint32_t process, uint32_t bytes) {
  return rt && process == rt->s7 && bytes == 0;
}

static int same_heap(const dc_goal_process_heap *heap, dc_goal_runtime *rt,
                     uint32_t process, uint32_t bytes) {
  if (root_scheduler(rt, process, bytes))
    return heap && !heap->runtime && !heap->process && !heap->allocation_bytes;
  return heap && heap->runtime == rt && heap->process == process && heap->allocation_bytes == bytes;
}

static int scheduler_open(dc_goal_runtime *rt, uint32_t process, uint32_t bytes,
                          dc_goal_process_heap *out) {
  if (root_scheduler(rt, process, bytes)) return 1;
  return open_reservation(rt, process, bytes, out);
}

static int span_bad(uint32_t base, uint32_t bytes) {
  return bytes && base > UINT32_MAX - bytes;
}

static int distinct_overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
  if (a == b && an == bn) return 0;
  if (a < 4 || b < 4 || span_bad(a - 4, an) || span_bad(b - 4, bn)) return 1;
  return overlap(a - 4, an, b - 4, bn);
}

/* process+36 is the PID word in the fiber receipt and the entry cases.
 * Mismatch fails closed. This file does not write that word. */
static int pid_read(dc_goal_runtime *rt, uint32_t process, uint32_t *pid) {
  unsigned char *p;
  if (process > UINT32_MAX - PP_PID_OFFSET) return 0;
  p = dc_goal_resolve(rt, process + PP_PID_OFFSET, 4);
  if (!p) return 0;
  memcpy(pid, p, 4);
  return 1;
}

static int scheduler_pid_read(dc_goal_runtime *rt, uint32_t process, uint32_t bytes, uint32_t *pid) {
  if (root_scheduler(rt, process, bytes)) { *pid = 0; return 1; }
  return pid_read(rt, process, pid);
}

static int stack_usable(const dc_goal_runtime *rt, const dc_goal_pp_scope_stack *stack) {
  return stack && storage_ok(rt, stack, sizeof *stack) && outside_fiber(stack, sizeof *stack);
}

static void drop_unsafe(dc_goal_pp_scope_stack *stack) {
  if (active_stack == stack) {active_stack = NULL;registration_runtime=NULL;}
}

static int chain_ok(dc_goal_pp_scope_stack *stack,dc_goal_runtime *rt,uint32_t context,
                    uint32_t scheduler,uint32_t scheduler_bytes,const dc_goal_process_heap *allocation) {
  dc_goal_process_heap checked;uint32_t pid;unsigned i;
  if(!stack->depth || stack->depth>DC_GOAL_PP_SCOPE_MAX_DEPTH ||
     !scheduler_word(rt,context,scheduler) ||
     !scheduler_open(rt,scheduler,scheduler_bytes,&checked) || !scheduler_pid_read(rt,scheduler,scheduler_bytes,&pid) ||
     pid!=stack->frames[0].scheduler_pid_witness)return 0;
  for(i=0;i<stack->depth;++i) {
    const dc_goal_pp_scope_frame *f=&stack->frames[i];
    uint32_t parent=i?stack->frames[i-1].lexical_process:scheduler;
    uint32_t bytes=i?stack->frames[i-1].lexical_bytes:scheduler_bytes;
    if(f->context!=context || f->scheduler_process!=scheduler || f->scheduler_bytes!=scheduler_bytes ||
       f->scheduler_pid_witness!=stack->frames[0].scheduler_pid_witness ||
       !same_heap(&f->saved_allocation,rt,parent,bytes) ||
       !open_reservation(rt,f->lexical_process,f->lexical_bytes,&checked) ||
       !pid_read(rt,f->lexical_process,&pid) || pid!=f->pid_witness)return 0;
  }
  return same_heap(allocation,rt,stack->frames[stack->depth-1].lexical_process,
                    stack->frames[stack->depth-1].lexical_bytes);
}

int dc_goal_pp_scope_init(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                          const dc_goal_pp_owner_hooks *hooks) {
  enum dc_goal_error prior;
  uint32_t owner = 0;
  dc_goal_pp_owner_hooks copied_hooks={0};
  if (!rt) return 0;
  prior = rt->error;
  if (!stack || !storage_ok(rt, stack, sizeof *stack) || !outside_fiber(stack, sizeof *stack))
    return fail(rt, prior);
  if (active_stack && active_stack != stack) return fail(rt, prior);
  if (active_stack == stack && (registration_runtime!=rt || stack->magic != PP_SCOPE_MAGIC || stack->runtime!=rt || stack->depth != 0))
    return fail(rt, prior);
  if(hooks)copied_hooks=*hooks; /* input may alias stack->hooks */
#ifdef __DREAMCAST__
  if(!copied_hooks.current_owner)return fail(rt,prior);
#endif
  if (copied_hooks.current_owner) {
    owner = copied_hooks.current_owner();
    if (!owner) return fail(rt, prior);
  }
  memset(stack, 0, sizeof *stack);
  stack->runtime = rt;
  stack->hooks = copied_hooks;
  stack->owner = owner;
  stack->magic = PP_SCOPE_MAGIC;
  active_stack = stack;
  registration_runtime=rt;
  rt->error = prior;
  return 1;
}

int dc_goal_pp_scope_enter(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                           uint32_t context, uint32_t scheduler_process,
                           uint32_t scheduler_bytes, uint32_t lexical_process,
                           uint32_t lexical_bytes,
                           const dc_goal_process_heap *current_allocation,
                           dc_goal_process_heap *installed_allocation) {
  enum dc_goal_error prior;
  dc_goal_process_heap saved, opened_scheduler, opened_lexical;
  dc_goal_pp_scope_frame *frame;
  dc_goal_fiber *seen = NULL;
  uint32_t pid = 0,scheduler_pid=0;
  unsigned i;
  if (!rt) return 0;
  prior = rt->error;
  if (prior != DC_GOAL_OK) return 0;
  if(stack!=active_stack || registration_runtime!=rt)return fail(rt,prior);
  if (!stack_usable(rt, stack) || stack != active_stack || stack->magic != PP_SCOPE_MAGIC ||
      stack->runtime != rt) {
    if (stack && active_stack == stack && !stack_usable(rt, stack)) drop_unsafe(stack);
    return fail(rt, prior);
  }
  if (stack->depth >= DC_GOAL_PP_SCOPE_MAX_DEPTH) return fail(rt, prior);
  if (!c_storage(rt, stack, current_allocation, sizeof *current_allocation) ||
      !c_storage(rt, stack, installed_allocation, sizeof *installed_allocation) ||
      !same_or_separate(current_allocation, installed_allocation, sizeof *current_allocation))
    return fail(rt, prior);
  if (!anchor_ok(stack, rt, stack->depth == 0, &seen)) return fail(rt, prior);
  if(seen && (seen->process!=scheduler_process || seen->reservation!=scheduler_bytes))return fail(rt,prior);
  if (!scheduler_word(rt, context, scheduler_process)) return fail(rt, prior);
  if (stack->depth == 0) {
    if (!same_heap(current_allocation, rt, scheduler_process, scheduler_bytes)) return fail(rt, prior);
  } else {
    if(!chain_ok(stack,rt,context,scheduler_process,scheduler_bytes,current_allocation))return fail(rt,prior);
  }
  if (distinct_overlap(scheduler_process, scheduler_bytes, lexical_process, lexical_bytes))
    return fail(rt, prior);
  for (i = 0; i < stack->depth; ++i) {
    if (distinct_overlap(stack->frames[i].lexical_process, stack->frames[i].lexical_bytes,
                         lexical_process, lexical_bytes)) return fail(rt, prior);
  }
  if (root_scheduler(rt, scheduler_process, scheduler_bytes) && seen) return fail(rt, prior);
  if (!scheduler_open(rt, scheduler_process, scheduler_bytes, &opened_scheduler)) return fail(rt, prior);
  if (!open_reservation(rt, lexical_process, lexical_bytes, &opened_lexical)) return fail(rt, prior);
  if (!pid_read(rt, lexical_process, &pid)) return fail(rt, prior);
  if (!scheduler_pid_read(rt, scheduler_process, scheduler_bytes, &scheduler_pid)) return fail(rt, prior);
  /* Mutation starts here. Failure paths above do not reach it. */
  saved = *current_allocation;
  frame = &stack->frames[stack->depth];
  memset(frame, 0, sizeof *frame);
  frame->context = context;
  frame->scheduler_process = scheduler_process;
  frame->scheduler_bytes = scheduler_bytes;
  frame->scheduler_pid_witness = scheduler_pid;
  frame->lexical_process = lexical_process;
  frame->lexical_bytes = lexical_bytes;
  frame->pid_witness = pid;
  frame->saved_allocation = saved;
  if (stack->depth == 0) {
    stack->fiber = seen;
    stack->anchor_direct = seen == NULL;
  }
  stack->depth++;
  *installed_allocation = opened_lexical;
  rt->error = DC_GOAL_OK;
  return 1;
}

int dc_goal_pp_scope_leave(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt,
                           uint32_t lexical_process, uint32_t lexical_bytes,
                           dc_goal_process_heap *restored_allocation) {
  enum dc_goal_error prior;
  dc_goal_pp_scope_frame *top;
  dc_goal_process_heap saved;
  if (!rt) return 0;
  prior = rt->error;
  if(stack!=active_stack || registration_runtime!=rt)return fail(rt,prior);
  if (!stack_usable(rt, stack) || stack != active_stack || stack->magic != PP_SCOPE_MAGIC ||
      stack->runtime != rt || stack->depth == 0 || stack->depth>DC_GOAL_PP_SCOPE_MAX_DEPTH) {
    if (stack && active_stack == stack && !stack_usable(rt, stack)) drop_unsafe(stack);
    return fail(rt, prior);
  }
  if (!c_storage(rt, stack, restored_allocation, sizeof *restored_allocation)) return fail(rt, prior);
  if (!anchor_ok(stack, rt, 0, NULL)) return fail(rt, prior);
  top = &stack->frames[stack->depth - 1];
  if (top->lexical_process != lexical_process || top->lexical_bytes != lexical_bytes) return fail(rt, prior);
  saved = top->saved_allocation;
  if(!same_heap(&saved,rt,stack->depth>1?stack->frames[stack->depth-2].lexical_process:top->scheduler_process,
                 stack->depth>1?stack->frames[stack->depth-2].lexical_bytes:top->scheduler_bytes))return fail(rt,prior);
  memset(top, 0, sizeof *top);
  stack->depth--;
  if (stack->depth == 0) {
    stack->fiber = NULL;
    stack->anchor_direct = 0;
  }
  *restored_allocation = saved;
  rt->error = prior;
  return 1;
}

int dc_goal_pp_scope_resolve(dc_goal_runtime *rt, uint32_t context,
                             uint32_t scheduler_process, uint32_t scheduler_bytes,
                             const dc_goal_process_heap *allocation,
                             uint32_t *lexical_process) {
  enum dc_goal_error prior;
  dc_goal_pp_scope_frame *top;
  if (!rt) return DC_GOAL_PP_SCOPE_DENIED;
  if (!active_stack || registration_runtime!=rt) return DC_GOAL_PP_SCOPE_ABSENT;
  if (!stack_usable(rt, active_stack)) {
    drop_unsafe(active_stack);
    if (rt->error == DC_GOAL_OK) rt->error = DC_GOAL_BAD_ARGUMENT;
    return DC_GOAL_PP_SCOPE_DENIED;
  }
  if (active_stack->runtime != rt) return deny(rt,rt->error);
  if (active_stack->magic != PP_SCOPE_MAGIC) return deny(rt, rt->error);
  if(active_stack->depth>DC_GOAL_PP_SCOPE_MAX_DEPTH)return deny(rt,rt->error);
  if (active_stack->depth == 0) return DC_GOAL_PP_SCOPE_ABSENT;
  prior = rt->error;
  top = &active_stack->frames[active_stack->depth - 1];
  /* The temporary output may live on the CURRENT native working stack, which
   * is inside rt's arena. It is not retained metadata. Valid disjoint C output
   * storage is the caller's contract; do not reject an authenticated fiber. */
  if (!lexical_process || !allocation || !outside_fiber(lexical_process,sizeof *lexical_process) ||
      overlap(address(lexical_process),sizeof *lexical_process,address(rt),sizeof *rt) ||
      overlap(address(lexical_process),sizeof *lexical_process,address(active_stack),sizeof *active_stack) ||
      !c_storage(rt,active_stack,allocation,sizeof *allocation) ||
      overlap(address(lexical_process),sizeof *lexical_process,address(allocation),sizeof *allocation))return deny(rt,prior);
  if (!anchor_ok(active_stack, rt, 0, NULL)) return deny(rt, prior);
  if(active_stack->fiber && (active_stack->fiber->process!=scheduler_process ||
      active_stack->fiber->reservation!=scheduler_bytes))return deny(rt,prior);
  if (top->context != context || top->scheduler_process != scheduler_process ||
      top->scheduler_bytes != scheduler_bytes) return deny(rt, prior);
  if(!chain_ok(active_stack,rt,context,scheduler_process,scheduler_bytes,allocation))return deny(rt,prior);
  *lexical_process = top->lexical_process;
  /* A retained fault stays through the scoped getter as well as this validator. */
  rt->error = prior;
  return DC_GOAL_PP_SCOPE_READY;
}

int dc_goal_pp_scope_blocks_transfer(const dc_goal_runtime *rt) {
  if (!rt || !active_stack || registration_runtime!=rt) return 0;
  if (!stack_usable(rt, active_stack)) return 1;
  if (active_stack->runtime != rt) return 1;
  if (active_stack->magic != PP_SCOPE_MAGIC) return 1;
  return active_stack->depth > 0;
}

int dc_goal_pp_scope_runtime_clear_allowed(const dc_goal_runtime *rt) {
  if (!rt) return 0;
  if (!active_stack || registration_runtime!=rt) return 1;
  if (!stack_usable(rt, active_stack)) return 0;
  if (active_stack->runtime != rt) return 0;
  return active_stack->magic == PP_SCOPE_MAGIC && active_stack->depth == 0;
}

unsigned dc_goal_pp_scope_depth(const dc_goal_runtime *rt) {
  if (!rt || !active_stack || registration_runtime!=rt || !stack_usable(rt, active_stack)) return 0;
  if (active_stack->runtime != rt || active_stack->magic != PP_SCOPE_MAGIC ||
      active_stack->depth>DC_GOAL_PP_SCOPE_MAX_DEPTH) return 0;
  return active_stack->depth;
}

dc_goal_pp_scope_stack *dc_goal_pp_scope_active(const dc_goal_runtime *rt) {
  if (!rt || !active_stack || registration_runtime!=rt || !stack_usable(rt, active_stack)) return NULL;
  if (active_stack->runtime != rt || active_stack->magic != PP_SCOPE_MAGIC ||
      active_stack->depth>DC_GOAL_PP_SCOPE_MAX_DEPTH) return NULL;
  return active_stack;
}

int dc_goal_pp_scope_detach(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt) {
  enum dc_goal_error prior;
  if (!rt) return 0;
  prior = rt->error;
  if (!stack || active_stack != stack || registration_runtime!=rt) return fail(rt, prior);
  if (!stack_usable(rt, stack)) {
    drop_unsafe(stack);
    return fail(rt, prior);
  }
  if (stack->runtime != rt || stack->magic != PP_SCOPE_MAGIC || stack->depth != 0) return fail(rt, prior);
  active_stack = NULL;
  registration_runtime=NULL;
  memset(stack, 0, sizeof *stack);
  rt->error = prior;
  return 1;
}

int dc_goal_pp_scope_abort(dc_goal_pp_scope_stack *stack, dc_goal_runtime *rt) {
  enum dc_goal_error prior;
  if (!rt || !stack) return 0;
  prior = rt->error;
  if((active_stack && registration_runtime!=rt) || dc_goal_fiber_current())return fail(rt,prior);
  if (active_stack && active_stack != stack) return fail(rt, prior);
  if (!stack_usable(rt, stack)) {
    if (active_stack == stack) drop_unsafe(stack);
    return fail(rt, prior);
  }
  /* A healthy stack for another runtime stays. Corrupt magic on the registered
   * stack still wipes, so clear cannot stick behind a bad word. */
  if (stack->runtime != rt && !(active_stack == stack && registration_runtime==rt))
    return fail(rt, prior);
  if(owner_now(stack)!=stack->owner)return fail(rt,prior);
  if (active_stack == stack) {active_stack = NULL;registration_runtime=NULL;}
  memset(stack, 0, sizeof *stack);
  stack->magic = PP_SCOPE_ABORTED;
  rt->error = prior;
  return 1;
}

int dc_goal_pp_scope_native_return(dc_goal_runtime *rt,dc_goal_process_heap *restored) {
  dc_goal_pp_scope_stack *stack=active_stack;dc_goal_process_heap saved;enum dc_goal_error prior;
  if(!rt || !stack || registration_runtime!=rt)return DC_GOAL_PP_SCOPE_ABSENT;
  prior=rt->error;
  if(!stack_usable(rt,stack))goto invalid;
  if(stack->runtime!=rt)goto invalid;
  if(stack->magic!=PP_SCOPE_MAGIC || stack->depth>DC_GOAL_PP_SCOPE_MAX_DEPTH)goto invalid;
  if(!stack->depth)return DC_GOAL_PP_SCOPE_ABSENT;
  if(!dc_goal_fiber_current() || !anchor_ok(stack,rt,0,NULL) ||
     !c_storage(rt,stack,restored,sizeof *restored))goto invalid;
  saved=stack->frames[0].saved_allocation;
  if(!same_heap(&saved,rt,stack->frames[0].scheduler_process,stack->frames[0].scheduler_bytes))goto invalid;
  memset(stack->frames,0,sizeof stack->frames);stack->depth=0;stack->fiber=NULL;stack->anchor_direct=0;
  *restored=saved;rt->error=prior;return DC_GOAL_PP_SCOPE_READY;
invalid:
  /* Only the fixed return hook uses this path. Never memset unsafe/corrupt
   * caller storage or try to execute a context stored in it. */
  active_stack=NULL;registration_runtime=NULL;deny(rt,prior);return DC_GOAL_PP_SCOPE_DENIED;
}
