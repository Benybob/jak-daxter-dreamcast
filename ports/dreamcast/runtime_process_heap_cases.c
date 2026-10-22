/* Original-created owners and explicit native process heap binding (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "process_heap_cases.h"
#include "build/process_heap_fixture.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
typedef uint64_t (*validator)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word(dc_goal_runtime *rt,uint32_t object,unsigned offset) {
  uint32_t v; memcpy(&v,dc_goal_resolve(rt,object+offset,4),4); return v;
}
static void set_word(dc_goal_runtime *rt,uint32_t object,unsigned offset,uint32_t value) {
  memcpy(dc_goal_resolve(rt,object+offset,4),&value,4);
}
uint32_t dc_runtime_process_heap_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                      unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result fixture,bridge;
  uint32_t old_object,old_process,old_tree,process_type=entry(rt,"process"),tree_type=entry(rt,"process-tree");
  uint32_t owner[3],base[3],objects[3],record,name,used,i,tree,cur,process_selector=rt->s7+DC_FIX_SYM_PROCESS_TYPE;
  const unsigned capacity[]={64,128,17};
  const uint64_t values[]={UINT64_C(0x12345678abcdef01),UINT64_C(0x8000000000000000),UINT64_MAX};
  one create,read,new_tree; two new_process,basic; three sized; validator valid;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  dc_process_heap_cases(check,context,id);
  CHECK(dc_goal_load_module(rt,goal_process_heap_fixture_exports,GOAL_PROCESS_HEAP_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture),
        1,"compiled process-selector allocation probes link");
  if(!fixture.functions) return 0;
  CHECK(fixture.functions,3,"three real GOAL process allocation functions");
  old_object=dc_goal_get_method(rt,rt->object_type,0);
  old_process=dc_goal_get_method(rt,process_type,0); old_tree=dc_goal_get_method(rt,tree_type,0);
  dc_goal_clear_process_heap(rt);
  CHECK(dc_goal_install_object_bridge(rt,sync,&bridge),1,"process-aware sized allocation bridge installed");
  if(!bridge.functions) return fixture.code_bytes;
  CHECK(dc_goal_set_method(rt,process_type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
  CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
  record=dc_goal_define_type(rt,"dc-process-heap-record",rt->basic_type,16,0,9);
  CHECK(record!=0,1,"record runtime type matches original compiler declaration");
  if(!record) return fixture.code_bytes+bridge.code_bytes;
  name=dc_goal_intern(rt,"dc-process-owner");
  new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
  for(i=0;i<3;++i) {
    owner[i]=(uint32_t)new_process(name,capacity[i]); base[i]=owner[i]+108;
    CHECK(owner[i]!=0,1,"original constructor creates bounded heap owner");
    if(!owner[i]) return fixture.code_bytes+bridge.code_bytes;
    CHECK(word(rt,owner[i],76),base[i],"original heap base used directly");
    CHECK(word(rt,owner[i],80),base[i]+capacity[i],"original constructor supplies exclusive heap top");
  }
  create=(one)(uintptr_t)entry(rt,"dc-process-heap-create"); read=(one)(uintptr_t)entry(rt,"dc-process-heap-read");
  new_tree=(one)(uintptr_t)entry(rt,"dc-process-heap-tree");
  basic=(two)(uintptr_t)entry(rt,"dc-c-basic-new"); sized=(three)(uintptr_t)entry(rt,"dc-c-object-new");
  valid=(validator)(uintptr_t)entry(rt,"valid?");
  used=rt->used;
  CHECK(basic(process_selector,record),0,"unbound process selector rejects without global fallback");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"unbound process selector explicit error");
  CHECK(rt->used,used,"unbound selector leaves global arena unchanged");
  CHECK(dc_goal_bind_process_heap(rt,owner[0],112+capacity[0]),1,"explicit known reservation bound");
  for(i=0;i<3;++i) {
    objects[i]=(uint32_t)create(values[i]);
    CHECK(objects[i],base[0]+i*16+4,"GOAL basic.new allocates in bound process heap");
    CHECK(read(objects[i]),values[i],"GOAL process record retains both halves");
    CHECK(dc_goal_type_of(rt,objects[i]),record,"process record has correct runtime type");
    CHECK(valid(objects[i],record,rt->s7,rt->s7,0),rt->s7+8,"unchanged original validator accepts process-allocated record");
    CHECK(word(rt,owner[0],84),base[0]+(i+1)*16,"original heap-cur advances through real GOAL allocation");
    CHECK(rt->used,used,"process record does not advance global arena");
  }
  CHECK(sized(process_selector,record,16),0,"process object.new exact fit rejects");
  CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"process exact fit explicit OOM");
  CHECK(word(rt,owner[0],84),base[0]+48,"process exact-fit cursor unchanged");
  CHECK(dc_goal_bind_process_heap(rt,owner[1],112+capacity[1]),1,"allocation target changes explicitly");
  tree=(uint32_t)new_tree(name);
  CHECK(tree,base[1]+4,"unchanged tree constructor object.new uses bound process heap");
  CHECK(dc_goal_type_of(rt,tree),tree_type,"process-owned original tree type");
  CHECK(word(rt,tree,4),256,"process-owned original tree mask");
  CHECK(word(rt,tree,20),tree+24,"process-owned original tree ppointer");
  CHECK(word(rt,owner[1],84),base[1]+32,"original tree consumes aligned process reservation");
  CHECK(create(77),base[1]+36,"record follows original tree in same process heap");
  CHECK(word(rt,owner[1],84),base[1]+48,"second process cursor independent");
  CHECK(rt->used,used,"switching heap targets never falls back globally");
  CHECK(dc_goal_bind_process_heap(rt,owner[0],112+capacity[0]),1,"former process reservation can be rebound");
  CHECK(word(rt,owner[0],84),base[0]+48,"rebinding preserves original cursor");
  for(i=0;i<3;++i) CHECK(read(objects[i]),values[i],"records survive allocation-target changes");
  CHECK(sized(process_selector,record,16),0,"rebound exact-fit request still rejects");
  CHECK(dc_goal_bind_process_heap(rt,owner[2],112+capacity[2]),1,"odd-capacity reservation binds");
  *(unsigned char*)dc_goal_resolve(rt,base[2]+16,1)=0x9c;
  CHECK(create(88),base[2]+4,"sixteen-byte record fits with one byte remaining");
  CHECK(word(rt,owner[2],84),base[2]+16,"odd-capacity original cursor remains aligned");
  CHECK(*(unsigned char*)dc_goal_resolve(rt,base[2]+16,1),0x9c,"aligned clear preserves unused tail byte");
  CHECK(basic(process_selector,record),0,"odd-capacity heap rejects another aligned record");
  CHECK(dc_goal_bind_process_heap(rt,owner[0],192),0,"bad explicit reservation cannot retain old target");
  CHECK(basic(process_selector,record),0,"failed binding clears former process target");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"failed binding remains explicit");
  CHECK(word(rt,owner[0],84),base[0]+48,"failed binding preserves original owner cursor");
  CHECK(rt->used,used,"failed binding never allocates globally");
  CHECK(dc_goal_bind_process_heap(rt,owner[1],112+capacity[1]),1,"valid process target binds again");
  cur=word(rt,owner[1],84); set_word(rt,owner[1],84,base[1]+1);
  CHECK(basic(process_selector,record),0,"bridge revalidates mutated owner metadata");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"mutated owner metadata explicit error");
  CHECK(word(rt,owner[1],84),base[1]+1,"bad metadata not silently rounded or overwritten");
  CHECK(rt->used,used,"mutated metadata has no global fallback");
  set_word(rt,owner[1],84,cur);
  dc_goal_clear_process_heap(rt);
  CHECK(basic(process_selector,record),0,"cleared process target rejects");
  rt->error=DC_GOAL_OK;
  CHECK(basic(rt->s7+DC_FIX_SYM_GLOBAL_HEAP,record)!=0,1,"explicit global path still works after clearing process target");
  CHECK(rt->used,used+16,"only explicit global request consumes global arena");
  CHECK(word(rt,owner[1],84),cur,"global allocation leaves process cursor unchanged");
  CHECK(word(rt,owner[0],32),dc_goal_find(rt,"dead"),"heap allocation never activates an owner");
  CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"sized allocation method restored");
  CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor binding restored");
  CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"tree constructor binding restored");
  CHECK(rt->error,DC_GOAL_OK,"bounded process heap integration leaves runtime clear");
#undef CHECK
  return fixture.code_bytes+bridge.code_bytes;
}
