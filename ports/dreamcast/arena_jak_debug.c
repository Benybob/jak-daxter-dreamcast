/* Execute original debug data initialization at its original load position.
 * Only cam-debug-h is enabled; debug gameplay remains off (AI-assisted), ISC. */
#ifdef DC_ARENA_JAK
#include "arena_jak.h"
#include "arena_live.h"
#include <string.h>

static uint32_t saved_debug,debug_symbol,used_before;
static int selected;
static int check(int ok,const char *why){return dc_arena_live_check(ok,1,why);}
static uint32_t named(dc_goal_runtime *rt,const char *name){
  uint32_t s=dc_goal_find(rt,name);return s?dc_goal_symbol_value(rt,s):0;
}
static int begin(dc_goal_runtime *rt,const char *name){
  dc_goal_code_entry_info info;
  if(!rt || rt->error!=DC_GOAL_OK)return 0;
  selected=name && !strcmp(name,"dc-game-cam-debug-h");
  if(!selected)return 1;
  debug_symbol=dc_goal_find(rt,"*debug-segment*");
  saved_debug=named(rt,"*debug-segment*");used_before=rt->used;
  dc_arena_jak_evidence.debug_entry=rt->temporary_entry;
  dc_arena_jak_evidence.debug_before=saved_debug;
  dc_arena_jak_evidence.layout_before=named(rt,"*cam-layout*");
  if(!check(rt->error==DC_GOAL_OK && !dc_arena_jak_evidence.debug_calls &&
            saved_debug==rt->s7 && dc_arena_jak_evidence.layout_before==0 &&
            dc_goal_code_entry(rt,rt->temporary_entry,&info) && info.symbol==rt->temporary_symbol &&
            info.symbol==dc_goal_find(rt,name),
            "jak original cam-debug-h startup is resident once, before default init"))return 0;
  return dc_goal_set_symbol(rt,debug_symbol,rt->s7+DC_FIX_SYM_TRUE);
}
static int end(dc_goal_runtime *rt,int ran){
  enum dc_goal_error error;
  unsigned i,j;uint32_t tables[5];int data_ok=1;
  static const char *const table_names[]={"*redline-table*","*blueline-table*","*greenline-table*","*yellowline-table*","*timeplot-table*"};
  static const char *const index_names[]={"*redline-index*","*blueline-index*","*greenline-index*","*yellowline-index*","*timeplot-index*"};
  if(!selected)return ran;
  /* Restore even if original startup faulted. Runtime setters clear error. */
  error=rt->error;
  dc_goal_set_symbol(rt,debug_symbol,saved_debug);
  rt->error=error;
  dc_arena_jak_evidence.debug_after=named(rt,"*debug-segment*");
  dc_arena_jak_evidence.debug_restored=dc_arena_jak_evidence.debug_after==saved_debug;
  dc_arena_jak_evidence.layout_after=named(rt,"*cam-layout*");
  dc_arena_jak_evidence.debug_bytes=rt->used-used_before;
  for(i=0;i<5;++i){
    tables[i]=named(rt,table_names[i]);
    if(tables[i]<rt->base+used_before || tables[i]>rt->base+rt->used ||
       rt->base+rt->used-tables[i]<1600 || named(rt,index_names[i])!=0)data_ok=0;
    for(j=0;j<i;++j)if(!(tables[i]+1600<=tables[j] || tables[j]+1600<=tables[i]))data_ok=0;
  }
  rt->error=error;
  if(!check(ran && error==DC_GOAL_OK && dc_arena_jak_evidence.debug_restored && data_ok &&
            dc_arena_jak_evidence.debug_bytes==8000,
            "jak original cam-debug-h allocates five tables and restores debug off"))return 0;
  ++dc_arena_jak_evidence.debug_calls;
  return check(dc_arena_jak_evidence.layout_after==rt->s7,
               "jak original define-perm supplies GOAL false camera layout default");
}
int dc_arena_jak_initializer(dc_goal_runtime *rt,const char *name,uint64_t *value){
  int ran;if(!begin(rt,name))return 0;
  ran=dc_goal_run_initializer(rt,name,value);return end(rt,ran);
}
int dc_arena_jak_initializer_void(dc_goal_runtime *rt,const char *name){
  int ran;if(!begin(rt,name))return 0;
  ran=dc_goal_run_initializer_void(rt,name);return end(rt,ran);
}
#endif
