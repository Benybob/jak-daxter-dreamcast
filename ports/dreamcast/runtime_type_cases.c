/* Compiled original deftype/defmethod paths on SH-4 (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_bridge.h"
#include "type_cases.h"
#include "build/type_fixture.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
typedef void (*two_void)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_compiled_type_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                       unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result bridges,module;
  uint32_t parent,child,a,b,score,alternate,used;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
#define ONE(name,a) ((one)(uintptr_t)entry(rt,name))((uint64_t)(a))
#define INSTALL(owner,fn) ((three)(uintptr_t)entry(rt,"dc-type-install"))(owner,9,fn)
  dc_runtime_type_cases(rt,check,context,id);
  if (!dc_goal_install_type_bridges(rt,sync,&bridges)) {
    CHECK(rt->error,0,"type bridges install"); return 0;
  }
  CHECK(bridges.functions,3,"three native type/allocation bridges");
  if (!dc_goal_load_module(rt,goal_type_fixture_exports,GOAL_TYPE_FIXTURE_EXPORT_COUNT,sync,NULL,&module)) {
    CHECK(rt->error,0,"type fixture load"); return 0;
  }
  CHECK(module.functions,11,"type method/functions plus startup exports");
  CHECK(entry(rt,"dc-native-record"),0,"linking defers runtime type creation");
  CHECK(dc_goal_run_initializer_void(rt,GOAL_TYPE_FIXTURE_INITIALIZER),1,"original deftype/defmethod startup");
  parent=entry(rt,"dc-native-record"); child=entry(rt,"dc-native-child");
  CHECK(dc_goal_type_of(rt,parent),rt->type_type,"compiled deftype creates original type object");
  CHECK(dc_goal_is_subtype(rt,child,parent),1,"compiled child ancestry");
  CHECK(dc_goal_is_subtype(rt,parent,rt->basic_type),1,"compiled class inherits basic");
  score=dc_goal_get_method(rt,parent,9); alternate=entry(rt,"dc-type-alternate");
  CHECK(dc_goal_type_of(rt,score),rt->function_type,"defmethod installs native function object");
  CHECK(dc_goal_get_method(rt,child,9),score,"child inherits compiled virtual method");
  CHECK(dc_goal_get_method(rt,child,0),dc_goal_get_method(rt,parent,0),"child inherits compiled constructor");
  a=(uint32_t)ONE("dc-type-create",23); b=(uint32_t)ONE("dc-type-child",(uint64_t)-4);
  CHECK(dc_goal_type_of(rt,a),parent,"compiled constructor allocates tagged parent");
  CHECK(dc_goal_type_of(rt,b),child,"inherited constructor retains requested child type");
  CHECK(ONE("dc-type-of",b),child,"typed field reads original runtime type header");
  CHECK(ONE("dc-type-read",a),23,"compiled constructor initializes original field offset");
  CHECK(ONE("dc-type-read",b),(uint64_t)-4,"inherited constructor signed field");
  CHECK(ONE("dc-type-score",a),32,"original virtual dispatch");
  CHECK(ONE("dc-type-score",b),5,"virtual dispatch through inherited method");
  ((two_void)(uintptr_t)entry(rt,"dc-type-write"))(a,(uint64_t)-20);
  CHECK(ONE("dc-type-score",a),(uint64_t)-11,"compiled typed mutation changes virtual result");
  dc_goal_set_symbol(rt,dc_goal_intern(rt,"*enable-method-set*"),0);
  CHECK(INSTALL(parent,alternate),alternate,"compiled method-set returns new function");
  CHECK(ONE("dc-type-score",a),80,"virtual method replacement is observed");
  CHECK(ONE("dc-type-score",b),5,"disabled propagation retains inherited child slot");
  INSTALL(parent,score);
  dc_goal_set_symbol(rt,dc_goal_find(rt,"*enable-method-set*"),1);
  CHECK(INSTALL(parent,alternate),alternate,"enabled method propagation");
  CHECK(ONE("dc-type-score",b),96,"child virtual dispatch observes propagated method");
  CHECK(INSTALL(child,score),score,"explicit child override installs original method");
  CHECK(INSTALL(parent,score),score,"parent method changed with override present");
  CHECK(ONE("dc-type-score",b),5,"child override remains usable");
  CHECK(INSTALL(child,0),0,"compiled method no-op control");
  CHECK(dc_goal_get_method(rt,child,9),score,"no-op leaves virtual slot");
  CHECK(INSTALL(child,1),0,"compiled method clear control");
  CHECK(dc_goal_get_method(rt,child,9),0,"clear leaves null virtual slot");
  CHECK(INSTALL(child,2),score,"compiled method inherit control");
  CHECK(ONE("dc-type-score",b),5,"inherited method executes after restore");
  used=rt->used;
  CHECK(dc_goal_run_initializer_void(rt,GOAL_TYPE_FIXTURE_INITIALIZER),1,"compiled type startup reruns");
  CHECK(entry(rt,"dc-native-record"),parent,"type startup preserves parent identity");
  CHECK(entry(rt,"dc-native-child"),child,"type startup preserves child identity");
  CHECK(rt->used,used,"type reinitialization does not leak allocations");
  CHECK(ONE("dc-type-score",a),(uint64_t)-11,"existing object remains valid after type reset");
  dc_goal_set_symbol(rt,dc_goal_find(rt,"*enable-method-set*"),0);
#undef INSTALL
#undef ONE
#undef CHECK
  return bridges.code_bytes+module.code_bytes;
}
