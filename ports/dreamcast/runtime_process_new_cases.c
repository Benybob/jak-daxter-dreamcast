/* Original constructors through bounded global object.new (AI-assisted), ISC. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/original_process_new.h"
#include "build/process_new_fixture.h"
typedef struct {
  uint32_t name,mask,parent,brother,child,ppointer,self,pool,status,pid;
  uint32_t main_thread,top_thread,entity,state,trans_hook,post_hook,event_hook;
  uint32_t allocated_length,next_state,heap_base,heap_top,heap_cur,stack_frame_top;
  unsigned char connection_list[16];
} process_layout;
typedef char process_layout_size[sizeof(process_layout)==108?1:-1];
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static process_layout *process_fields(dc_goal_runtime *rt,uint32_t process) {
  return dc_goal_resolve(rt,process,sizeof(process_layout));
}
uint32_t dc_runtime_process_new_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                     unsigned *id,dc_goal_cache_sync sync) {
  const unsigned false_offsets[]={offsetof(process_layout,pool),offsetof(process_layout,main_thread),
    offsetof(process_layout,top_thread),offsetof(process_layout,entity),offsetof(process_layout,state),
    offsetof(process_layout,next_state),offsetof(process_layout,trans_hook),offsetof(process_layout,post_hook),
    offsetof(process_layout,event_hook),offsetof(process_layout,stack_frame_top),offsetof(process_layout,parent),
    offsetof(process_layout,brother),offsetof(process_layout,child)};
  const unsigned sizes[]={0,1,15,16,31,64,256,1024};
  const unsigned char zero[124]={0};
  dc_goal_module_result original,bridge,fixture;
  uint32_t process_type=entry(rt,"process"),tree_type=entry(rt,"process-tree");
  uint32_t global=rt->s7+DC_FIX_SYM_GLOBAL_HEAP,old_object,old_process,old_tree;
  uint32_t raw,root,processes[8],name,root_name,used,symbols,dead;
  uint64_t bad_heap[10],bad_type[10],bad_size[10];
  enum dc_goal_error errors[10];
  three allocate; one new_tree; two new_process,reparent,search; unsigned i,j;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_original_process_new_exports,GOAL_ORIGINAL_PROCESS_NEW_EXPORT_COUNT,sync,NULL,&original),
        1,"unchanged original process constructors link without startup");
  CHECK(dc_goal_load_module(rt,goal_process_new_fixture_exports,GOAL_PROCESS_NEW_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture),
        1,"compiled native constructor dispatch wrappers link");
  if(!original.functions || !fixture.functions) return 0;
  CHECK(original.functions,2,"two unchanged original constructor methods");
  CHECK(fixture.functions,3,"two real dispatch wrappers and original-search predicate");
  old_object=dc_goal_get_method(rt,rt->object_type,0);
  old_tree=dc_goal_get_method(rt,tree_type,0); old_process=dc_goal_get_method(rt,process_type,0);
  CHECK(dc_goal_install_object_bridge(rt,sync,&bridge),1,"explicit global-sized object bridge installation");
  if(!bridge.functions) return original.code_bytes+fixture.code_bytes;
  CHECK(bridge.functions,1,"one actual bounded allocation bridge");
  CHECK(dc_goal_get_method(rt,rt->object_type,0),entry(rt,"dc-c-object-new"),"original object.new dispatch uses native sized allocation");
  CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor installed at new slot");
  CHECK(dc_goal_set_method(rt,process_type,0,entry(rt,"(method new process)")),1,"original process constructor installed at new slot");
  allocate=(three)(uintptr_t)entry(rt,"dc-c-object-new");
  used=rt->used; raw=(uint32_t)allocate(global,process_type,128);
  CHECK(raw!=0,1,"bounded sized basic allocation");
  if(!raw) return original.code_bytes+fixture.code_bytes+bridge.code_bytes;
  CHECK(dc_goal_type_of(rt,raw),process_type,"sized allocation writes original type header");
  CHECK(rt->used-used,128,"sized allocation reserves requested aligned bytes");
  CHECK((raw-4)&15u,0,"sized allocation header is sixteen-byte aligned");
  CHECK(memcmp(dc_goal_resolve(rt,raw,124),zero,124),0,"sized allocation zeroes body");
  symbols=rt->symbol_count;
  for(i=0;i<10;++i) { bad_heap[i]=global; bad_type[i]=process_type; bad_size[i]=128; errors[i]=DC_GOAL_BAD_ARGUMENT; }
  bad_heap[0]=global+8; bad_heap[1]=(UINT64_C(1)<<32)|global;
  bad_type[2]=0; bad_type[3]=rt->object_type; bad_type[4]=(UINT64_C(1)<<32)|process_type;
  bad_size[5]=0; bad_size[6]=111; bad_size[7]=UINT64_C(0x100000000);
  bad_size[8]=UINT32_MAX; bad_size[9]=rt->capacity; errors[9]=DC_GOAL_OUT_OF_MEMORY;
  for(i=0;i<10;++i) {
    used=rt->used; rt->error=DC_GOAL_OK;
    CHECK(allocate(bad_heap[i],bad_type[i],bad_size[i]),0,"unsupported/invalid/OOM allocation returns no object");
    CHECK(rt->error,errors[i],"allocation failure has explicit bounded error");
    CHECK(rt->used,used,"rejected allocation leaves arena unchanged");
  }
  CHECK(rt->symbol_count,symbols,"allocation guards create no symbol bindings");
  rt->error=DC_GOAL_OK;
  name=dc_goal_intern(rt,"dc-created-process"); root_name=dc_goal_intern(rt,"dc-created-root"); dead=dc_goal_intern(rt,"dead");
  new_tree=(one)(uintptr_t)entry(rt,"dc-new-tree"); new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
  used=rt->used; root=(uint32_t)new_tree(root_name);
  CHECK(root!=0,1,"original tree constructor via compiled GOAL method dispatch");
  if(!root) return original.code_bytes+fixture.code_bytes+bridge.code_bytes;
  CHECK(dc_goal_type_of(rt,root),tree_type,"original tree constructor runtime type");
  CHECK(rt->used-used,32,"original tree constructor reserves original size");
  { uint32_t *f=dc_goal_resolve(rt,root,28);
    CHECK(f[0],root_name,"original tree name");
    CHECK(f[1],256,"original organizational tree mask");
    CHECK(f[2],rt->s7,"original tree parent false");
    CHECK(f[3],rt->s7,"original tree brother false");
    CHECK(f[4],rt->s7,"original tree child false");
    CHECK(f[5],root+24,"original tree stable ppointer");
    CHECK(f[6],root,"original tree self");
  }
  for(i=0;i<8;++i) {
    process_layout *p;
    used=rt->used; processes[i]=(uint32_t)new_process(name,sizes[i]);
    CHECK(processes[i]!=0,1,"original process constructor via compiled GOAL method dispatch");
    if(!processes[i]) return original.code_bytes+fixture.code_bytes+bridge.code_bytes;
    p=process_fields(rt,processes[i]);
    CHECK(dc_goal_type_of(rt,processes[i]),process_type,"original process runtime type");
    CHECK(rt->used-used,(112u+sizes[i]+15u)&~15u,"original process reserves header, fields and requested heap");
    CHECK(p->name,name,"original process name");
    CHECK(p->mask,0,"bounded zeroing supplies initial process mask");
    CHECK(p->status,dead,"original process starts dead");
    CHECK(p->pid,0,"original process pid awaits activation");
    CHECK(p->allocated_length,sizes[i],"original process records requested dynamic size");
    CHECK(p->heap_base,processes[i]+108,"original process dynamic storage begins after fields");
    CHECK(p->heap_cur,p->heap_base,"original process heap starts empty");
    CHECK(p->heap_top,p->heap_base+sizes[i],"original process heap has requested exclusive end");
    CHECK(p->heap_base&15u,0,"original dynamic storage starts sixteen-byte aligned");
    CHECK(p->ppointer,processes[i]+24,"original process ppointer addresses self field");
    CHECK(p->self,processes[i],"original process self initialized");
    for(j=0;j<sizeof(false_offsets)/sizeof(false_offsets[0]);++j) {
      uint32_t value;
      memcpy(&value,(const unsigned char*)p+false_offsets[j],4);
      CHECK(value,rt->s7,"original process empty state/thread/tree field is false");
    }
    CHECK(memcmp(p->connection_list,zero,16),0,"bounded zeroing supplies untouched inline connection bytes");
    CHECK(dc_goal_resolve(rt,p->heap_base,sizes[i])!=NULL,1,"entire original reserved dynamic span is within arena");
  }
  reparent=(two)(uintptr_t)entry(rt,"change-parent"); search=(two)(uintptr_t)entry(rt,"search-process-tree");
  CHECK(reparent(processes[0],root),processes[0],"constructed process attaches using original change-parent");
  CHECK(process_fields(rt,processes[0])->parent,root+24,"constructed process uses original stable parent handle");
  CHECK(((uint32_t*)dc_goal_resolve(rt,root,28))[4],processes[0]+24,"constructed root points at first process handle");
  CHECK(reparent(processes[7],root),processes[7],"second constructed process attaches");
  CHECK(((uint32_t*)dc_goal_resolve(rt,root,28))[4],processes[7]+24,"original constructor and reparenting integrate");
  CHECK(process_fields(rt,processes[7])->brother,processes[0]+24,"constructed sibling chain uses stable handles");
  CHECK(process_fields(rt,processes[0])->ppointer,processes[0]+24,"constructed handle remains stable");
  CHECK(process_fields(rt,processes[7])->status,dead,"tree attachment does not activate a process");
  CHECK(dc_goal_set_symbol(rt,dc_goal_find(rt,"*dc-new-target*"),processes[0]),1,"compiled search predicate target set");
  CHECK(search(root,entry(rt,"dc-new-match")),processes[0],"original search finds original-constructed process through GOAL callback");
  CHECK(dc_goal_set_symbol(rt,dc_goal_find(rt,"*dc-new-target*"),root),1,"search target changes to constructed organizational node");
  CHECK(search(root,entry(rt,"dc-new-match")),rt->s7,"original search excludes constructed organizational node");
  CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object allocation test binding restored");
  CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"tree constructor test binding restored");
  CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor test binding restored");
  CHECK(rt->error,DC_GOAL_OK,"original constructors leave runtime clear");
#undef CHECK
  return original.code_bytes+fixture.code_bytes+bridge.code_bytes;
}
