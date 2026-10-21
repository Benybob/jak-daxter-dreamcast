/* Shared original type/new and method-set! contract (AI-assisted), ISC. */
#include "type_cases.h"
#include <string.h>
static uint64_t flags(unsigned bytes,unsigned heap,unsigned methods) {
  return (uint64_t)bytes|((uint64_t)heap<<16)|((uint64_t)methods<<32);
}
static dc_goal_type_fields *fields(dc_goal_runtime *rt,uint32_t type) {
  return dc_goal_resolve(rt,type,sizeof(dc_goal_type_fields));
}
void dc_runtime_type_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id) {
  uint32_t parent,child,grand,other,symbol,f1,f2,f3,used,capacity,original;
  dc_goal_type_fields *p;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  f1=dc_goal_new_basic(rt,rt->function_type,4); f2=dc_goal_new_basic(rt,rt->function_type,4);
  f3=dc_goal_new_basic(rt,rt->function_type,4);
  original=dc_goal_get_method(rt,rt->basic_type,5);
  dc_goal_set_method(rt,rt->basic_type,5,f1);
  symbol=dc_goal_intern(rt,"dc-unit-type-parent");
  parent=dc_goal_new_type(rt,symbol,rt->basic_type,flags(20,7,12));
  CHECK(parent!=0,1,"new type binds symbol");
  if (!parent) return;
  CHECK(dc_goal_symbol_value(rt,symbol),parent,"new type symbol identity");
  CHECK(dc_goal_type_of(rt,parent),rt->type_type,"new type runtime tag");
  p=fields(rt,parent);
  CHECK(p->parent,rt->basic_type,"new type parent");
  CHECK(p->allocated_size,20,"new type size flags");
  CHECK(p->padded_size,32,"new type padded size");
  CHECK(p->heap_base,7,"new type heap flags");
  CHECK(p->num_methods,12,"new type method flags");
  CHECK(dc_goal_get_method(rt,parent,5),f1,"new type inherits defined parent method");
  CHECK(dc_goal_get_method(rt,parent,11),0,"child-only slots zeroed");
  child=dc_goal_new_type(rt,dc_goal_intern(rt,"dc-unit-type-child"),parent,flags(24,9,14));
  grand=dc_goal_new_type(rt,dc_goal_intern(rt,"dc-unit-type-grand"),child,flags(24,9,14));
  other=dc_goal_new_type(rt,dc_goal_intern(rt,"dc-unit-type-other"),rt->basic_type,flags(16,0,14));
  CHECK(child && grand && other,1,"three more type definitions");
  CHECK(dc_goal_get_method(rt,child,13),0,"bounded child inheritance beyond parent count");
  CHECK(dc_goal_method_set(rt,parent,5,0,1),0,"method zero is no-op");
  CHECK(dc_goal_get_method(rt,parent,5),f1,"no-op preserves method");
  CHECK(dc_goal_method_set(rt,parent,5,f2,0),f2,"method replacement returns function");
  CHECK(dc_goal_get_method(rt,child,5),f1,"propagation disabled leaves child unchanged");
  dc_goal_method_set(rt,parent,5,f1,0);
  dc_goal_method_set(rt,child,5,f3,0);
  CHECK(dc_goal_method_set(rt,parent,5,f2,1),f2,"propagated method replacement");
  CHECK(dc_goal_get_method(rt,child,5),f3,"explicit child override preserved");
  CHECK(dc_goal_get_method(rt,grand,5),f2,"matching descendant method updated");
  CHECK(dc_goal_get_method(rt,other,5),f1,"unrelated type with same method preserved");
  CHECK(dc_goal_method_set(rt,child,5,2,0),f2,"inherit sentinel returns parent method");
  CHECK(dc_goal_get_method(rt,child,5),f2,"inherit sentinel installs parent value");
  CHECK(dc_goal_method_set(rt,parent,5,1,1),0,"clear sentinel returns null");
  CHECK(rt->error,DC_GOAL_OK,"clear null result is successful");
  CHECK(dc_goal_get_method(rt,grand,5),0,"clear propagates to inherited descendant");
  CHECK(dc_goal_get_method(rt,child,5),0,"clear propagates to child");
  CHECK(dc_goal_method_set(rt,parent,12,f1,0),0,"method index outside declared table rejected");
  CHECK(rt->error,DC_GOAL_METHOD_CAPACITY,"method bounds explicit error");
  CHECK(dc_goal_method_set(rt,parent,5,3,0),0,"non-function method rejected");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"non-function method error");
  CHECK(dc_goal_method_set(rt,child,13,2,0),0,"inherit outside parent capacity rejected");
  CHECK(rt->error,DC_GOAL_METHOD_CAPACITY,"inherited method bounds error");
  used=rt->used;
  CHECK(dc_goal_new_type(rt,symbol,rt->basic_type,flags(28,11,12)),parent,"type redefinition reuses object");
  CHECK(rt->used,used,"type redefinition does not allocate");
  CHECK(fields(rt,parent)->allocated_size,28,"type redefinition updates size");
  CHECK(fields(rt,parent)->heap_base,11,"type redefinition updates heap metadata");
  CHECK(dc_goal_get_method(rt,parent,5),f1,"type redefinition resets inherited methods");
  dc_goal_set_method(rt,grand,5,f1);
  fields(rt,grand)->parent=grand;
  CHECK(dc_goal_method_set(rt,parent,5,f2,1),0,"cyclic descendant aborts method propagation");
  CHECK(rt->error,DC_GOAL_BAD_TYPE,"corrupt hierarchy has explicit method error");
  CHECK(dc_goal_get_method(rt,parent,5),f1,"failed propagation preserves source method");
  CHECK(dc_goal_get_method(rt,grand,5),f1,"failed propagation preserves child method");
  fields(rt,grand)->parent=child;
  CHECK(dc_goal_new_type(rt,symbol,rt->basic_type,flags(32,0,13)),0,"type method capacity cannot grow in place");
  CHECK(rt->error,DC_GOAL_METHOD_CAPACITY,"type growth explicit error");
  CHECK(fields(rt,parent)->allocated_size,28,"failed growth preserves metadata");
  CHECK(dc_goal_new_type(rt,symbol,grand,flags(32,0,12)),0,"cyclic reparenting rejected");
  CHECK(fields(rt,parent)->parent,rt->basic_type,"cyclic reparenting leaves parent unchanged");
  CHECK(dc_goal_new_type(rt,symbol,rt->s7,flags(20,0,12)),0,"non-type parent rejected");
  CHECK(dc_goal_new_type(rt,rt->s7,rt->basic_type,flags(20,0,12)),0,"occupied non-type symbol rejected");
  CHECK(dc_goal_new_type(rt,symbol,rt->basic_type,flags(65535,0,12)),0,"padded size overflow rejected");
  CHECK(dc_goal_new_type(rt,symbol,rt->basic_type,flags(20,0,127)),0,"oversize method count rejected");
  symbol=dc_goal_intern(rt,"dc-unit-default-methods");
  other=dc_goal_new_type(rt,symbol,rt->basic_type,flags(4,0,0));
  CHECK(other!=0 && fields(rt,other)->num_methods==12,1,"zero flag count selects original twelve-method default");
  symbol=dc_goal_intern(rt,"dc-unit-type-oom"); used=rt->used; capacity=rt->capacity;
  rt->capacity=rt->used;
  CHECK(dc_goal_new_type(rt,symbol,rt->basic_type,flags(8,0,12)),0,"type allocation exhaustion");
  CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"type allocation error preserved");
  CHECK(rt->used,used,"failed type allocation consumes no bytes");
  CHECK(dc_goal_symbol_value(rt,symbol),0,"failed type allocation leaves binding empty");
  rt->capacity=capacity;
  dc_goal_set_method(rt,rt->basic_type,5,original);
#undef CHECK
}
