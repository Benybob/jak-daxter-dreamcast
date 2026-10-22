/* Unchanged original process-tree algorithms on native fixtures (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/original_process_tree.h"
typedef struct { uint32_t name,mask,parent,brother,child,ppointer,self; } tree_prefix;
typedef char tree_prefix_size_check[sizeof(tree_prefix)==28?1:-1];
enum { A=0,B=1,C=2,D=3,ROOT=4,TREE=5 };
typedef struct {
  dc_goal_runtime *rt;
  uint32_t nodes[6],context,dead,wanted,dead_node,mutate,blocked,count,logic_count,error;
  uint64_t trace;
  int search;
} tree_fixture;
static tree_fixture *active;
static tree_prefix *fields(tree_fixture *f,unsigned i) {
  return dc_goal_resolve(f->rt,f->nodes[i],sizeof(tree_prefix));
}
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static unsigned node_id(tree_fixture *f,uint32_t object) {
  unsigned i;
  for(i=0;i<6;++i) if(f->nodes[i]==object) return i+1;
  return 0;
}
static uint64_t callback(uint64_t object) {
  tree_fixture *f=active;
  tree_prefix *node;
  unsigned id;
  if(!f || object>UINT32_MAX) return 0;
  node=dc_goal_resolve(f->rt,(uint32_t)object,sizeof(tree_prefix));
  if(!node) { f->error=1; return f->rt->s7; }
  id=node_id(f,(uint32_t)object);
  if(id<1 || id>6 || f->count>=8) { f->error=1; return f->rt->s7; }
  f->trace|=(uint64_t)id<<(8*f->count++);
  if(f->mutate==(uint32_t)object) node->brother=f->rt->s7;
  if(f->search) return (uint32_t)object==f->wanted?f->rt->s7+8:f->rt->s7;
  return (uint32_t)object==f->dead_node?f->dead:f->rt->s7+8;
}
static uint64_t run_logic(uint64_t object) {
  tree_fixture *f=active;
  tree_prefix *node;
  unsigned id;
  if(!f || object>UINT32_MAX) return 0;
  node=dc_goal_resolve(f->rt,(uint32_t)object,sizeof(tree_prefix));
  if(!node) { f->error=1; return f->rt->s7; }
  id=node_id(f,(uint32_t)object); ++f->logic_count;
  if(id<1 || id>4) { f->error=1; return f->rt->s7; }
  return (f->blocked&(1u<<(id-1)))?f->rt->s7:f->rt->s7+8;
}
static void reset_trace(tree_fixture *f) {
  f->wanted=f->dead_node=f->mutate=f->blocked=f->count=f->logic_count=0;
  f->trace=0; f->search=0;
}
static void reset_tree(tree_fixture *f) {
  static const char *const names[]={"dc-tree-a","dc-tree-b","dc-tree-c","dc-tree-d","dc-tree-root","dc-tree-group"};
  unsigned i;
  for(i=0;i<6;++i) {
    tree_prefix *p=fields(f,i);
    p->name=dc_goal_intern(f->rt,names[i]);
    if(!p->name) f->error=1;
    p->mask=i>=ROOT?256:0;
    p->parent=p->brother=p->child=f->rt->s7;
    p->self=f->nodes[i]; p->ppointer=f->nodes[i]+24;
  }
  /* ROOT -> A(C), TREE(D), B. Links point at stable self fields. */
  fields(f,ROOT)->child=fields(f,A)->ppointer;
  fields(f,A)->brother=fields(f,TREE)->ppointer;
  fields(f,TREE)->brother=fields(f,B)->ppointer;
  fields(f,A)->parent=fields(f,TREE)->parent=fields(f,B)->parent=fields(f,ROOT)->ppointer;
  fields(f,A)->child=fields(f,C)->ppointer; fields(f,C)->parent=fields(f,A)->ppointer;
  fields(f,TREE)->child=fields(f,D)->ppointer; fields(f,D)->parent=fields(f,TREE)->ppointer;
  { uint32_t zero=0; memcpy(dc_goal_resolve(f->rt,f->context,4),&zero,4); }
}
uint32_t dc_runtime_process_tree_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                      unsigned *id,dc_goal_cache_sync sync) {
  typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
  typedef uint64_t (*two)(uint64_t,uint64_t);
  const unsigned order[]={A,C,D,B};
  const uint32_t masks[]={0,1,64,4};
  const uint64_t all=UINT64_C(0x02040301);
  dc_goal_module_result original,bridge;
  unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  dc_goal_code_export exports[2]={{"dc-tree-callback",code[0],12,NULL,0},{"dc-tree-run-logic",code[1],12,NULL,0}};
  tree_fixture f;
  three iterate,execute; two search,reparent;
  uint32_t process_type,tree_type,context_type,target,visit,old_method,prevent,eligible,count;
  uint64_t trace,result; unsigned i,j,blocked,bits;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_original_process_tree_exports,GOAL_ORIGINAL_PROCESS_TREE_EXPORT_COUNT,sync,NULL,&original),
        1,"unchanged original process-tree functions link");
  if(!original.functions) return 0;
  target=(uint32_t)(uintptr_t)callback; memcpy(code[0]+8,&target,4);
  target=(uint32_t)(uintptr_t)run_logic; memcpy(code[1]+8,&target,4);
  CHECK(dc_goal_load_module(rt,exports,2,sync,NULL,&bridge),1,"boxed native tree callback probes link");
  if(!bridge.functions) return original.code_bytes;
  CHECK(original.functions,4,"four original process-tree functions");
  CHECK(bridge.functions,2,"two real conditional native callbacks");
  process_type=entry(rt,"process"); tree_type=entry(rt,"process-tree");
  CHECK(((dc_goal_type_fields*)dc_goal_resolve(rt,process_type,sizeof(dc_goal_type_fields)))->allocated_size,112,"original process allocation schema");
  CHECK(((dc_goal_type_fields*)dc_goal_resolve(rt,tree_type,sizeof(dc_goal_type_fields)))->allocated_size,32,"original process-tree allocation schema");
  context_type=dc_goal_define_type(rt,"kernel-context",rt->basic_type,48,0,9);
  CHECK(context_type!=0,1,"bounded context fixture matches original declared size");
  memset(&f,0,sizeof(f)); f.rt=rt; f.dead=dc_goal_intern(rt,"dead");
  for(i=0;i<6;++i) f.nodes[i]=dc_goal_new_basic(rt,i>=ROOT?tree_type:process_type,i>=ROOT?32:112);
  f.context=context_type?dc_goal_new_basic(rt,context_type,48):0;
  CHECK(f.nodes[0] && f.nodes[1] && f.nodes[2] && f.nodes[3] && f.nodes[4] && f.nodes[5] && f.context && f.dead,
        1,"original-layout process tree fixtures allocate");
  if(!f.nodes[0] || !f.nodes[1] || !f.nodes[2] || !f.nodes[3] || !f.nodes[4] || !f.nodes[5] || !f.context || !f.dead)
    return original.code_bytes+bridge.code_bytes;
  old_method=dc_goal_get_method(rt,process_type,12);
  CHECK(dc_goal_set_method(rt,process_type,12,entry(rt,"dc-tree-run-logic")),1,"conditional run-logic method installed in original slot12");
  visit=entry(rt,"dc-tree-callback");
  iterate=(three)(uintptr_t)entry(rt,"iterate-process-tree"); execute=(three)(uintptr_t)entry(rt,"execute-process-tree");
  search=(two)(uintptr_t)entry(rt,"search-process-tree"); reparent=(two)(uintptr_t)entry(rt,"change-parent");
  reset_tree(&f); active=&f;
  CHECK(iterate(f.nodes[ROOT],visit,f.context),rt->s7+8,"original iteration returns tree-node truth");
  CHECK(f.count,4,"original iteration skips organizational tree nodes");
  CHECK(f.trace,all,"original depth-first child/brother traversal");
  CHECK(f.logic_count,0,"plain original iteration does not dispatch run-logic");
  prevent=UINT32_MAX; memcpy(dc_goal_resolve(rt,f.context,4),&prevent,4); reset_trace(&f);
  CHECK(iterate(f.nodes[ROOT],visit,f.context),rt->s7+8,"plain iteration ignores context prevention");
  CHECK(f.count,4,"plain iteration still visits every process with full prevention");
  CHECK(f.trace,all,"plain iteration context does not change order");
  for(i=0;i<4;++i) fields(&f,i)->mask=masks[i];
  for(bits=0;bits<8;++bits) for(blocked=0;blocked<16;++blocked) {
    prevent=((bits&1)?1u:0u)|((bits&2)?64u:0u)|((bits&4)?4u:0u);
    memcpy(dc_goal_resolve(rt,f.context,4),&prevent,4);
    reset_trace(&f); f.blocked=blocked; trace=0; count=eligible=0;
    for(j=0;j<4;++j) {
      unsigned node=order[j];
      if(!(masks[node]&prevent)) {
        ++eligible;
        if(!(blocked&(1u<<node))) trace|=(uint64_t)(node+1)<<(8*count++);
      }
    }
    result=execute(f.nodes[ROOT],visit,f.context);
    CHECK(result,rt->s7+8,"original execution retains tree-node result");
    CHECK(f.logic_count,eligible,"original context prevention short-circuits run-logic");
    CHECK(f.count,count,"original execution combines mask and conditional method");
    CHECK(f.trace,trace,"blocked parent still traverses eligible children in original order");
  }
  reset_tree(&f);
  for(i=0;i<4;++i) {
    reset_trace(&f); f.dead_node=f.nodes[i];
    result=iterate(f.nodes[ROOT],visit,f.context);
    CHECK(result,rt->s7+8,"dead child return does not change root result");
    CHECK(f.count,i==A?3:4,"dead callback prunes only its own children");
    CHECK(f.trace,i==A?UINT64_C(0x020401):all,"dead subtree still permits later siblings");
  }
  reset_trace(&f); f.dead_node=f.nodes[A];
  CHECK(iterate(f.nodes[A],visit,f.context),f.dead,"original non-tree root returns callback dead symbol");
  CHECK(f.count,1,"dead root prunes its child");
  CHECK(f.trace,1,"dead root callback runs exactly once");
  for(i=0;i<6;++i) {
    reset_trace(&f); f.search=1; f.wanted=f.nodes[i];
    result=search(f.nodes[ROOT],visit);
    count=i==A?1:i==C?2:i==D?3:4;
    trace=count==4?all:(all&((UINT64_C(1)<<(count*8))-1));
    CHECK(result,i>=ROOT?rt->s7:f.nodes[i],"original search returns first matching process, excludes tree nodes");
    CHECK(f.count,count,"original search stops at first match");
    CHECK(f.trace,trace,"original search traverses depth first");
  }
  reset_trace(&f); f.mutate=f.nodes[A];
  CHECK(iterate(f.nodes[ROOT],visit,f.context),rt->s7+8,"callback may mutate its own sibling link");
  CHECK(f.trace,all,"original traversal saved sibling before recursive callback");
  CHECK(fields(&f,A)->brother,rt->s7,"mutation actually changed fixture link");
  reset_tree(&f);
  CHECK(reparent(f.nodes[C],f.nodes[TREE]),f.nodes[C],"original reparent returns moved node");
  CHECK(fields(&f,A)->child,rt->s7,"reparent removes old first/only child");
  CHECK(fields(&f,TREE)->child,fields(&f,C)->ppointer,"reparent inserts at new parent's front");
  CHECK(fields(&f,C)->brother,fields(&f,D)->ppointer,"reparent retains existing new siblings");
  CHECK(fields(&f,C)->parent,fields(&f,TREE)->ppointer,"reparent uses parent ppointer");
  CHECK(reparent(f.nodes[B],f.nodes[TREE]),f.nodes[B],"original reparent removes old last sibling");
  CHECK(fields(&f,TREE)->brother,rt->s7,"old parent's predecessor is spliced");
  CHECK(fields(&f,TREE)->child,fields(&f,B)->ppointer,"moved last node becomes new first child");
  CHECK(fields(&f,B)->brother,fields(&f,C)->ppointer,"new child list retains following sibling");
  CHECK(fields(&f,B)->parent,fields(&f,TREE)->ppointer,"moved node has new parent handle");
  CHECK(reparent(f.nodes[A],f.nodes[TREE]),f.nodes[A],"original reparent removes old first sibling");
  CHECK(fields(&f,ROOT)->child,fields(&f,TREE)->ppointer,"old parent child advances");
  CHECK(fields(&f,TREE)->child,fields(&f,A)->ppointer,"new parent child advances to moved first");
  CHECK(fields(&f,A)->brother,fields(&f,B)->ppointer,"moved first precedes existing children");
  CHECK(reparent(f.nodes[TREE],f.nodes[ROOT]),f.nodes[TREE],"same-parent reparent preserves a valid tree");
  CHECK(fields(&f,ROOT)->child,fields(&f,TREE)->ppointer,"same-parent node remains first");
  CHECK(fields(&f,TREE)->brother,rt->s7,"same-parent reparent adds no self-cycle");
  CHECK(reparent(f.nodes[D],f.nodes[A]),f.nodes[D],"original reparent splices a later sibling");
  CHECK(fields(&f,C)->brother,rt->s7,"later sibling predecessor now ends list");
  CHECK(fields(&f,A)->child,fields(&f,D)->ppointer,"later sibling attached under new process parent");
  CHECK(fields(&f,D)->parent,fields(&f,A)->ppointer,"later sibling parent stable handle");
  CHECK(fields(&f,D)->brother,rt->s7,"later sibling moves to empty child list");
  reset_trace(&f);
  CHECK(iterate(f.nodes[ROOT],visit,f.context),rt->s7+8,"mutated original tree traverses");
  CHECK(f.count,4,"all processes remain reachable after reparenting");
  CHECK(f.trace,UINT64_C(0x03020401),"original iteration follows new child/brother links");
  for(i=0;i<6;++i) {
    CHECK(fields(&f,i)->self,f.nodes[i],"stable self identity survives tree operations");
    CHECK(fields(&f,i)->ppointer,f.nodes[i]+24,"stable ppointer survives tree operations");
  }
  CHECK(f.error,0,"all native callbacks receive allocated original-layout objects");
  CHECK(dc_goal_set_method(rt,process_type,12,old_method),1,"conditional test method restored");
  CHECK(rt->error,DC_GOAL_OK,"original tree algorithms leave runtime clear");
  active=NULL;
#undef CHECK
  return original.code_bytes+bridge.code_bytes;
}
