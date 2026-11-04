/* Preserve the first authentication fault and all query receipts (AI-assisted), ISC. */
#include "arena_jak_exports.h"
#include <string.h>
volatile dc_jak_exports_evidence dc_arena_jak_exports_evidence={.version=1};
typedef char export_receipt_bytes[sizeof(dc_jak_exports_evidence)==520?1:-1];
int dc_arena_jak_exports(dc_goal_runtime *rt,unsigned stage){
  static const char *const names[]={"start","kernel-dispatcher","read-pad","init-target"};
  dc_jak_export_snapshot snapshot;
  enum dc_goal_error first=DC_GOAL_OK;
  unsigned i;
  if(!rt || stage>=2)return 0;
  /* No lookup may clear an existing original-code fault. */
  if(rt->error!=DC_GOAL_OK){dc_arena_jak_exports_evidence.prior_error=rt->error;return 0;}
  memset(&snapshot,0,sizeof snapshot);snapshot.used=rt->used;snapshot.capacity=rt->capacity;
  for(i=0;i<DC_JAK_EXPORT_COUNT;++i){
    dc_jak_export_row *row=&snapshot.exports[i];dc_goal_code_entry_info info;
    row->symbol=dc_goal_find(rt,names[i]);
    row->value=row->symbol?dc_goal_symbol_value(rt,row->symbol):0;
    if(rt->error==DC_GOAL_OK && dc_goal_code_entry_diagnose(rt,row->value,&info,&row->diagnostic)){
      row->bytes=info.bytes;row->owner=info.symbol;
      if(info.symbol!=row->symbol){rt->error=DC_GOAL_BAD_FUNCTION;row->diagnostic.reason=DC_CODE_EXPORT_SYMBOL;}
    }
    row->error=rt->error;
    if(!row->value && !row->error)row->error=DC_GOAL_BAD_FUNCTION;
    if(row->error && !snapshot.first_failed){snapshot.first_failed=i+1;first=(enum dc_goal_error)row->error;}
  }
  dc_arena_jak_exports_evidence.stages[stage]=snapshot;
  dc_arena_jak_exports_evidence.completed|=1u<<stage;
  if(snapshot.first_failed && !dc_arena_jak_exports_evidence.first_failed_stage)
    dc_arena_jak_exports_evidence.first_failed_stage=stage+1;
  rt->error=first;
  return !snapshot.first_failed;
}
