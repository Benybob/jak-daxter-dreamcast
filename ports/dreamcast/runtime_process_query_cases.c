/* Original queries, anonymous callbacks and string/name equality (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/original_process_query.h"
#include "build/original_name_compare.h"
#include "build/original_null_context.h"
#include "build/original_query_globals.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef struct { uint32_t name,mask,parent,brother,child,ppointer,self; } tree_fields;
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static tree_fields *fields(dc_goal_runtime *rt,uint32_t node) {
  return dc_goal_resolve(rt,node,sizeof(tree_fields));
}
/* Independent fixture DFS uses indices, without following original ppointers. */
static int next_node(unsigned node,unsigned step) {
  static const int order[6][5]={{0,2,-1,-1,-1},{1,-1,-1,-1,-1},{2,-1,-1,-1,-1},
                                {3,-1,-1,-1,-1},{0,2,3,1,-1},{3,-1,-1,-1,-1}};
  return order[node][step];
}
uint32_t dc_runtime_process_query_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                       unsigned *id,dc_goal_cache_sync sync) {
  const char *texts[]={"dc-name-alpha","dc-name-alpha","dc-name-alpha","dc-name-beta","dc-name-beta",
                       "dc-name-gamma","","","prefix","prefix-long","byte-\x80\xff","dc-name-alpha"};
  const unsigned node_names[]={0,4,2,5,8,9};
  uint32_t values[12],nodes[6],old_object,old_process,old_tree,process_type=entry(rt,"process"),tree_type=entry(rt,"process-tree");
  uint32_t null_ctx,used,expected,expected_not,count,search_name,search_count;
  unsigned i,j,k; int candidate;
  dc_goal_module_result queries,compare,null_context,globals;
  uint64_t result;
  unsigned char before[6][28],zero_bytes[44]={0};
  one count_process,new_tree;
  two by_name,not_name,names_equal,strings_equal,new_process,reparent;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_original_name_compare_exports,GOAL_ORIGINAL_NAME_COMPARE_EXPORT_COUNT,
                           sync,NULL,&compare),1,"unchanged original string/name comparison bodies link");
  CHECK(dc_goal_load_module(rt,goal_original_process_query_exports,GOAL_ORIGINAL_PROCESS_QUERY_EXPORT_COUNT,
                           sync,NULL,&queries),1,"original process queries and their three anonymous callbacks link");
  CHECK(dc_goal_load_module_data(rt,goal_original_null_context_exports,GOAL_ORIGINAL_NULL_CONTEXT_EXPORT_COUNT,
        GOAL_ORIGINAL_NULL_CONTEXT_DATA,GOAL_ORIGINAL_NULL_CONTEXT_DATA_COUNT,sync,NULL,&null_context),1,
        "unchanged original null-context definition links");
  CHECK(dc_goal_load_module(rt,goal_original_query_globals_exports,GOAL_ORIGINAL_QUERY_GLOBALS_EXPORT_COUNT,
                           sync,NULL,&globals),1,"unchanged original search-global range links");
  if(!compare.functions || !queries.functions || !null_context.functions || !globals.functions) return 0;
  CHECK(compare.functions,2,"two unchanged original comparison routines");
  CHECK(queries.functions,6,"three queries retain three real original anonymous functions");
  CHECK(null_context.data_bytes,48,"original null context static image size");
  CHECK(dc_goal_run_initializer(rt,GOAL_ORIGINAL_NULL_CONTEXT_INITIALIZER,&result),1,"explicit original null context initialization");
  null_ctx=entry(rt,"*null-kernel-context*");
  CHECK(memcmp(dc_goal_resolve(rt,null_ctx,44),zero_bytes,44),0,"all original null context fields remain zero");
  CHECK(dc_goal_run_initializer(rt,GOAL_ORIGINAL_QUERY_GLOBALS_INITIALIZER,&result),1,"explicit original search globals initialization");
  CHECK(entry(rt,"*global-search-name*"),rt->s7,"original search name starts false");
  CHECK(entry(rt,"*global-search-count*"),0,"original search count starts zero");
  search_name=dc_goal_find(rt,"*global-search-name*"); search_count=dc_goal_find(rt,"*global-search-count*");
  by_name=(two)(uintptr_t)entry(rt,"process-by-name"); not_name=(two)(uintptr_t)entry(rt,"process-not-name");
  count_process=(one)(uintptr_t)entry(rt,"process-count");
  names_equal=(two)(uintptr_t)entry(rt,"name="); strings_equal=(two)(uintptr_t)entry(rt,"string=");
  for(i=0;i<12;++i) {
    values[i]=(i==0 || i==3 || i==5)?dc_goal_intern(rt,texts[i]):dc_goal_string(rt,texts[i]);
    CHECK(values[i]!=0,1,"symbol/string comparison fixture allocates");
  }
  /* Original equality uses NUL termination, not allocated-length. */
  values[11]=dc_goal_string(rt,"dc-name-alphaXXX");
  *(unsigned char*)dc_goal_resolve(rt,values[11]+4+13,1)=0;
  for(i=0;i<12;++i) for(j=0;j<12;++j) {
    expected=!strcmp(texts[i],texts[j])?rt->s7+8:rt->s7;
    CHECK(names_equal(values[i],values[j]),expected,"original name= agrees with independent byte equality across symbols/strings");
    if(i!=0 && i!=3 && i!=5 && j!=0 && j!=3 && j!=5)
      CHECK(strings_equal(values[i],values[j]),expected,"original string= covers distinct allocations, prefixes, high bytes and NUL termination");
  }
  CHECK(strings_equal(0,values[1]),rt->s7,"original string= rejects zero first reference before dereference");
  CHECK(strings_equal(values[1],0),rt->s7,"original string= rejects zero second reference before dereference");
  CHECK(strings_equal(0,0),rt->s7,"original string= zero references remain false");
  old_object=dc_goal_get_method(rt,rt->object_type,0); old_process=dc_goal_get_method(rt,process_type,0);
  old_tree=dc_goal_get_method(rt,tree_type,0);
  CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"bounded sized constructor selected");
  CHECK(dc_goal_set_method(rt,process_type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
  CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
  dc_goal_clear_process_context(rt);
  new_tree=(one)(uintptr_t)entry(rt,"dc-new-tree"); new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
  reparent=(two)(uintptr_t)entry(rt,"change-parent");
  for(i=0;i<6;++i) {
    nodes[i]=(uint32_t)(i<4?new_process(values[node_names[i]],32):new_tree(values[node_names[i]]));
    CHECK(nodes[i]!=0,1,"queries use unchanged original process/tree constructors");
    if(!nodes[i]) return compare.code_bytes+queries.code_bytes+globals.code_bytes+null_context.code_bytes;
  }
  CHECK(reparent(nodes[1],nodes[4]),nodes[1],"original reparent appends first root child");
  CHECK(reparent(nodes[5],nodes[4]),nodes[5],"original reparent adds organization node");
  CHECK(reparent(nodes[0],nodes[4]),nodes[0],"original reparent adds first DFS process");
  CHECK(reparent(nodes[2],nodes[0]),nodes[2],"original reparent creates process child");
  CHECK(reparent(nodes[3],nodes[5]),nodes[3],"original reparent creates group child");
  for(i=0;i<6;++i) memcpy(before[i],fields(rt,nodes[i]),28);
  used=rt->used;
  for(i=0;i<6;++i) {
    count=0;
    for(k=0;k<5 && next_node(i,k)>=0;++k) ++count;
    CHECK(count_process(nodes[i]),count,"original count includes processes and skips organizational nodes");
    CHECK(dc_goal_symbol_value(rt,search_count),count,"original anonymous counter updates original global cell");
    for(j=0;j<12;++j) {
      expected=expected_not=rt->s7;
      for(k=0;k<5 && (candidate=next_node(i,k))>=0;++k) {
        int match=!strcmp(texts[node_names[candidate]],texts[j]);
        if(match && expected==rt->s7) expected=nodes[candidate];
        if(!match && expected_not==rt->s7) expected_not=nodes[candidate];
      }
      CHECK(by_name(values[j],nodes[i]),expected,"original process-by-name returns first DFS symbol/string name match");
      CHECK(dc_goal_symbol_value(rt,search_name),values[j],"original query sets its exact search-name reference");
      CHECK(not_name(values[j],nodes[i]),expected_not,"original process-not-name returns first DFS nonmatch");
    }
  }
  /* A mask change prunes the node's own count/match, not its child subtree. */
  fields(rt,nodes[0])->mask|=256;
  CHECK(count_process(nodes[4]),3,"tree-mask mutation excludes only that process node from count");
  CHECK(by_name(values[0],nodes[4]),nodes[2],"tree-mask mutation exposes later equal-name child");
  fields(rt,nodes[0])->mask&=~256u;
  for(i=0;i<6;++i) CHECK(memcmp(before[i],fields(rt,nodes[i]),28),0,"all query/count operations preserve original tree links and names");
  CHECK(rt->used,used,"original queries/count/comparisons allocate no heap storage");
  CHECK(memcmp(dc_goal_resolve(rt,null_ctx,44),zero_bytes,44),0,"query count never mutates original null context");
  CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object constructor binding restored");
  CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor binding restored");
  CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"tree constructor binding restored");
  CHECK(rt->error,DC_GOAL_OK,"original query integration leaves runtime clear");
#undef CHECK
  return compare.code_bytes+queries.code_bytes+globals.code_bytes+null_context.code_bytes;
}
