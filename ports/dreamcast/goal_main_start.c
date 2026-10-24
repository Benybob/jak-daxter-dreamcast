/* Original arming and explicit SH4 startup adaptation (AI-assisted), ISC. */
#include "goal_main_start.h"
#include "goal_main_thread.h"
#include "goal_bridge.h"
#include "goal_scalar_call.h"
#include <string.h>

static int bad(dc_goal_runtime *rt){if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
static int overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){
 return an && bn && (a<=b?b-a<an:a-b<bn);
}
static int canonical(dc_goal_runtime *rt,const char *name,dc_goal_code_entry_info *code){
 uint32_t symbol=dc_goal_find(rt,name),entry=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(!dc_goal_code_entry(rt,entry,code))return 0;
 if(code->symbol!=symbol){rt->error=DC_GOAL_BAD_FUNCTION;return 0;}
 return 1;
}
/* The process reservation is the complete possible write owner. Its namespace,
 * schemas and every selected code/tag span must remain outside that owner. */
static int spans(dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                  uint32_t stack,uint32_t bytes,const dc_goal_code_entry_info *code,unsigned count){
 uint32_t types[3],i,symbol;dc_goal_type_fields schema;
 types[0]=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE);
 types[1]=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_THREAD_TYPE);
 symbol=dc_goal_find(rt,"cpu-thread");types[2]=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(overlap(process-4,reservation,rt->table,DC_GOAL_SYMBOL_BYTES) ||
    overlap(stack,bytes,rt->table,DC_GOAL_SYMBOL_BYTES))return bad(rt);
 for(i=0;i<3;++i){
  const void *p=dc_goal_resolve(rt,types[i],sizeof schema);if(!p)return 0;
  memcpy(&schema,p,sizeof schema);
  if(types[i]<4 || overlap(process-4,reservation,types[i]-4,20u+4u*schema.num_methods) ||
     overlap(stack,bytes,types[i]-4,20u+4u*schema.num_methods))return bad(rt);
 }
 for(i=0;i<count;++i)
  if(overlap(process-4,reservation,code[i].entry-4,code[i].bytes+4) ||
     overlap(stack,bytes,code[i].entry-4,code[i].bytes+4))return bad(rt);
 return 1;
}
int dc_goal_arm_main_thread(dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                            uint32_t thread,uint32_t stack,uint32_t bytes,uint32_t target,const uint64_t arguments[6]){
 dc_goal_code_entry_info code[3];uint64_t words[8],result;unsigned i;
 if(!rt || !dc_goal_core_bridges_active(rt) || dc_goal_fiber_current() || !arguments)return bad(rt);
 if(!dc_goal_main_thread_check(rt,process,reservation,thread,stack,bytes,0,0))return 0;
 if(!canonical(rt,"set-to-run",&code[0]) || !canonical(rt,"set-to-run-bootstrap",&code[1]) ||
    !dc_goal_code_entry(rt,target,&code[2]))return 0;
 if(!spans(rt,process,reservation,stack,bytes,code,3))return 0;
 words[0]=thread;words[1]=target;for(i=0;i<6;++i)words[i+2]=arguments[i];
 rt->error=DC_GOAL_OK;
 if(!dc_goal_scalar_call(code[0].entry,8,words,&result))return bad(rt);
 return rt->error==DC_GOAL_OK;
}
static int invalidate(dc_goal_fiber *f,dc_goal_runtime *rt,enum dc_goal_error error){
 /* Reuse init_words' owned-storage alias/active checks and inactive invalidation. */
 dc_goal_fiber_init_words(f,rt,0,0,0,0,0,0,9,NULL);
 if(rt)rt->error=error;
 return 0;
}
int dc_goal_fiber_init_main(dc_goal_fiber *f,dc_goal_runtime *rt,uint32_t process,
                            uint32_t reservation,uint32_t thread,uint32_t stack,uint32_t bytes){
 dc_goal_code_entry_info code[2];uint64_t target,arguments[6];const void *p;
 if(dc_goal_fiber_current() || !f)return bad(rt);
 if(!rt || !dc_goal_core_bridges_active(rt))return invalidate(f,rt,DC_GOAL_BAD_ARGUMENT);
 if(!canonical(rt,"set-to-run-bootstrap",&code[0]) ||
    !dc_goal_main_thread_check(rt,process,reservation,thread,stack,bytes,1,code[0].entry))
  return invalidate(f,rt,rt->error);
 p=dc_goal_resolve(rt,thread+36,56);if(!p)return invalidate(f,rt,rt->error);
 memcpy(&target,p,8);memcpy(arguments,(const unsigned char *)p+8,sizeof arguments);
 if(target>UINT32_MAX)return invalidate(f,rt,DC_GOAL_BAD_FUNCTION);
 if(!dc_goal_code_entry(rt,(uint32_t)target,&code[1]))return invalidate(f,rt,rt->error);
 if(!spans(rt,process,reservation,stack,bytes,code,2))return invalidate(f,rt,rt->error);
 if(!dc_goal_fiber_init_words(f,rt,process,reservation,thread,stack,bytes,(uint32_t)target,6,arguments))return 0;
 p=dc_goal_resolve(rt,process+36,4);if(!p)return invalidate(f,rt,rt->error);
 memcpy(&f->main_import_pid,p,4);
 if(!f->main_import_pid)return invalidate(f,rt,DC_GOAL_BAD_ARGUMENT);
 f->main_import_self=(uintptr_t)f;return 1;
}
int dc_goal_main_completion_check(dc_goal_fiber *f,dc_goal_runtime *rt){
 dc_goal_code_entry_info code[2];uint64_t words[7];uint32_t backup;const void *p;
 if(!dc_goal_fiber_main_completed(f,rt))return 0;
 if(!dc_goal_core_bridges_active(rt))return bad(rt);
 if(!canonical(rt,"set-to-run-bootstrap",&code[0]) ||
    !dc_goal_main_thread_check(rt,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,1,code[0].entry))return 0;
 p=dc_goal_resolve(rt,f->thread+32,4);if(!p)return 0;memcpy(&backup,p,4);
 if(backup!=f->image.backup_bytes || f->arguments[6] || f->arguments[7])return bad(rt);
 p=dc_goal_resolve(rt,f->thread+36,56);if(!p)return 0;memcpy(words,p,sizeof words);
 if(words[0]!=(uint64_t)f->entry || memcmp(words+1,f->arguments,48))return bad(rt);
 if(!dc_goal_code_entry(rt,f->entry,&code[1]))return 0;
 if(!spans(rt,f->process,f->reservation,f->stack_base,f->stack_bytes,code,2))return 0;
 rt->error=DC_GOAL_OK;return 1;
}
int dc_goal_main_completion_release(dc_goal_fiber *f,dc_goal_runtime *rt){
 if(!dc_goal_main_completion_check(f,rt))return 0;
 return dc_goal_fiber_clear(f);
}
