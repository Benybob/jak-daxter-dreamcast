/* HOST doubles for original-call boundaries, never GOAL execution.
 * Native host stubs are registered through the real loader (AI-assisted), ISC. */
#include "arena_jak.h"
#include "arena_live.h"
#include "goal_fiber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
static unsigned checks,passed,failed,init_calls;
static int init_mode,call_mode;
static dc_goal_runtime rt;
static unsigned char memory[524288] __attribute__((aligned(4096)));
volatile dc_live_evidence dc_arena_live_evidence;
static uint32_t cp,dir,names,data,art;
static const char *failure;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
dc_goal_fiber *dc_goal_fiber_current(void){return NULL;}
int dc_arena_live_check(uint64_t a,uint64_t b,const char *why){
  if(a==b){++passed;return 1;}++failed;failure=why;return 0;
}
static uint32_t sym(const char *s,uint32_t value){
  uint32_t p=dc_goal_intern(&rt,s);CHECK(p && dc_goal_set_symbol(&rt,p,value));return p;
}
static void put(uint32_t p,uint32_t v){void *b=dc_goal_resolve(&rt,p,4);CHECK(b);memcpy(b,&v,4);}
static uint32_t get(uint32_t p){uint32_t v;const void *b=dc_goal_resolve(&rt,p,4);CHECK(b);memcpy(&v,b,4);return v;}
static uint32_t basic(const char *name,unsigned bytes){
  uint32_t t=dc_goal_find(&rt,name),v;
  if(!t || !(v=dc_goal_symbol_value(&rt,t)))v=dc_goal_define_type(&rt,name,rt.basic_type,(uint16_t)bytes,0,16);
  CHECK(v);v=dc_goal_new_basic(&rt,v,bytes);CHECK(v);return v;
}
static uint64_t fake_continue(uint64_t game){
  CHECK((uint32_t)game==get(dc_goal_find(&rt,"*game-info*")));
  if(call_mode==1){rt.error=DC_GOAL_BAD_OBJECT;return cp;}
  if(call_mode==6)put(dc_goal_find(&rt,"*target*"),cp);
  return call_mode==2?rt.s7:cp;
}
static uint64_t fake_lookup(uint64_t d,uint64_t name,uint64_t reload,uint64_t heap,uint64_t version){
  CHECK(d==dir && name==get(get(dc_goal_find(&rt,"*jchar-sg*"))) && reload==rt.s7 && heap==0x1234 && version==1);
  if(call_mode==3)rt.error=DC_GOAL_BAD_FUNCTION;
  if(call_mode==4)put(names,1);
  if(call_mode==7)put(dc_goal_find(&rt,"*target*"),art);
  return call_mode==5?art:rt.s7;
}
static uint64_t forbidden_spawn(void){CHECK(0);return 0;}
static void install(const char *name,uintptr_t target,int temporary){
  /* x86-64 movabs rax, target; jmp rax. This is host fixture code only. */
  unsigned char code[12]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
  uint64_t address=target;dc_goal_module_result loaded;
  dc_goal_code_export f={name,code,sizeof code,NULL,0};memcpy(code+2,&address,8);
  CHECK(dc_goal_load_module_temporary(&rt,&f,1,NULL,0,temporary?name:NULL,NULL,NULL,&loaded));
}
static void setup(void){
  CHECK((uintptr_t)memory<=UINT32_MAX);
  CHECK(dc_goal_init(&rt,memory,sizeof memory,(uint32_t)(uintptr_t)memory));
  memset((void *)&dc_arena_jak_evidence,0,sizeof dc_arena_jak_evidence);dc_arena_jak_evidence.version=1;
  memset((void *)&dc_arena_live_evidence,0,sizeof dc_arena_live_evidence);
  passed=failed=init_calls=0;failure=NULL;init_mode=call_mode=0;
  sym("*debug-segment*",rt.s7);sym("*cam-layout*",0);
}
/* The test initializer simulates stores/allocations, so it tests the wrapper's
 * contracts, not original initialization. A separate typed-IR test covers that. */
int dc_goal_run_initializer(dc_goal_runtime *r,const char *name,uint64_t *value){
  static const char *const ts[]={"*redline-table*","*blueline-table*","*greenline-table*","*yellowline-table*","*timeplot-table*"};
  static const char *const is[]={"*redline-index*","*blueline-index*","*greenline-index*","*yellowline-index*","*timeplot-index*"};
  unsigned i;CHECK(r==&rt);++init_calls;
  if(strcmp(name,"dc-game-cam-debug-h")){if(value)*value=42;return 1;}
  CHECK(get(dc_goal_find(r,"*debug-segment*"))==r->s7+DC_FIX_SYM_TRUE);
  for(i=0;i<5;++i){sym(ts[i],dc_goal_alloc(r,1600));sym(is[i],0);}
  if(init_mode!=1)sym("*cam-layout*",r->s7);
  if(init_mode==2){r->error=DC_GOAL_OUT_OF_MEMORY;return 0;}
  if(init_mode==3)sym(ts[4],get(dc_goal_find(r,ts[0])));
  if(value)*value=0;
  return 1;
}
int dc_goal_run_initializer_void(dc_goal_runtime *r,const char *name){return dc_goal_run_initializer(r,name,NULL);}
static void debug_setup(void){
  unsigned i;
  /* Real stream linking interns relocation symbols before the initializer. */
  static const char *const syms[]={"*redline-table*","*blueline-table*","*greenline-table*","*yellowline-table*","*timeplot-table*",
                                 "*redline-index*","*blueline-index*","*greenline-index*","*yellowline-index*","*timeplot-index*"};
  setup();for(i=0;i<10;++i)sym(syms[i],0);
  install("dc-game-cam-debug-h",(uintptr_t)forbidden_spawn,1);
}
static void probe_setup(void){
  uint32_t levels,levtype,sg,load;unsigned char *b;
  setup();
  dc_arena_jak_evidence.debug_calls=dc_arena_jak_evidence.debug_restored=1;
  dc_arena_live_evidence.restored=1;dc_arena_live_evidence.rendered=DC_LIVE_UPDATES;
  dc_arena_live_evidence.turning_pairs=2;
  install("start",(uintptr_t)forbidden_spawn,0);install("init-target",(uintptr_t)forbidden_spawn,0);
  install("(method get-or-create-continue! game-info)",(uintptr_t)fake_continue,0);
  install("(method load-to-heap-by-name load-dir-art-group)",(uintptr_t)fake_lookup,0);
  sym("*target*",rt.s7);sym("*target-dead-pool*",basic("test-pool",32));sym("*target-pool*",basic("test-pool",32));
  sym("*game-info*",basic("game-info",128));load=dc_goal_alloc(&rt,48);sym("*load-state*",load);
  cp=basic("continue-point",128);sym("*default-continue*",cp);
  levels=basic("level-group",0x1ef4);sym("*level*",levels);
  levtype=dc_goal_define_type(&rt,"level",rt.basic_type,2608,0,16);CHECK(levtype);put(levels+5312-4,levtype);
  sg=basic("skeleton-group",80);sym("*jchar-sg*",sg);put(sg,dc_goal_string(&rt,"eichar"));
  b=dc_goal_resolve(&rt,sg+60,2);CHECK(b);b[0]=2;b[1]=1;
  dir=basic("load-dir-art-group",16);put(levels+5312+48,dir);
  CHECK(dc_goal_set_method(&rt,get(dir-4),9,get(dc_goal_find(&rt,"(method load-to-heap-by-name load-dir-art-group)"))));
  names=basic("array",16+50*4);data=basic("array",16+50*4);
  put(dir+4,names);put(dir+8,data);put(names,0);put(data,0);put(names+4,50);put(data+4,50);
  art=basic("art-group",32);sym("global",0x1234);
  /* Flush/reset is a no-op for coherent x86; memory must be executable. */
#ifdef _WIN32
  {DWORD old;CHECK(VirtualProtect(memory,sizeof memory,PAGE_EXECUTE_READWRITE,&old));}
#else
  CHECK(mprotect(memory,sizeof memory,PROT_READ|PROT_WRITE|PROT_EXEC)==0);
#endif
}
int main(void){
  uint64_t v;
  setup();CHECK(dc_arena_jak_initializer(&rt,"other",&v) && v==42 && init_calls==1 && !passed);
  debug_setup();CHECK(dc_arena_jak_initializer(&rt,"dc-game-cam-debug-h",&v));
  CHECK(passed==3 && !failed && dc_arena_jak_evidence.debug_calls==1 && dc_arena_jak_evidence.debug_bytes==8000);
  CHECK(get(dc_goal_find(&rt,"*debug-segment*"))==rt.s7 && get(dc_goal_find(&rt,"*cam-layout*"))==rt.s7);
  CHECK(!dc_arena_jak_initializer(&rt,"dc-game-cam-debug-h",&v) && init_calls==1);
  debug_setup();init_mode=1;CHECK(!dc_arena_jak_initializer(&rt,"dc-game-cam-debug-h",&v));
  CHECK(failed==1 && strstr(failure,"false camera layout"));
  debug_setup();init_mode=2;CHECK(!dc_arena_jak_initializer(&rt,"dc-game-cam-debug-h",&v));
  CHECK(rt.error==DC_GOAL_OUT_OF_MEMORY && dc_arena_jak_evidence.debug_restored && !dc_arena_jak_evidence.debug_calls);
  debug_setup();init_mode=3;CHECK(!dc_arena_jak_initializer_void(&rt,"dc-game-cam-debug-h") && !dc_arena_jak_evidence.debug_calls);
  probe_setup();CHECK(!dc_arena_jak_finish(&rt));
  CHECK(passed==9 && failed==1 && dc_arena_jak_evidence.blocker==10 && strstr(failure,"missing eichar"));
  CHECK(dc_arena_jak_evidence.continue_calls==1 && dc_arena_jak_evidence.lookup_calls==1 &&
        dc_arena_jak_evidence.target_before==rt.s7 && dc_arena_jak_evidence.target_after==rt.s7 &&
        dc_arena_jak_evidence.art_result==rt.s7 && !dc_arena_jak_evidence.spawn_calls &&
        !dc_arena_jak_evidence.target_resumes && !dc_arena_jak_evidence.target_state &&
        !dc_arena_jak_evidence.pad_position_updates && !dc_arena_jak_evidence.follow_frames);
  probe_setup();call_mode=1;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==5 && rt.error==DC_GOAL_BAD_OBJECT);
  probe_setup();call_mode=2;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==5);
  probe_setup();call_mode=3;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==9 && rt.error==DC_GOAL_BAD_FUNCTION);
  probe_setup();call_mode=4;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==9);
  probe_setup();call_mode=5;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==11 && !dc_arena_jak_evidence.spawn_calls);
  probe_setup();call_mode=6;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==5 &&
                               dc_arena_jak_evidence.target_after==cp && !dc_arena_jak_evidence.lookup_calls);
  probe_setup();call_mode=7;CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==9 &&
                               dc_arena_jak_evidence.target_after==art && !dc_arena_jak_evidence.spawn_calls);
  probe_setup();put(names,51);CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==8 && !dc_arena_jak_evidence.lookup_calls);
  probe_setup();sym("*target*",0);CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==3 && !dc_arena_jak_evidence.continue_calls);
  probe_setup();put(get(dc_goal_find(&rt,"*level*"))+5312+48,rt.s7);
  CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==7 && !dc_arena_jak_evidence.lookup_calls);
  probe_setup();CHECK(dc_goal_set_method(&rt,get(dir-4),9,get(dc_goal_find(&rt,"start"))));
  CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==7 && !dc_arena_jak_evidence.lookup_calls);
  probe_setup();sym("start",0);CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==2);
  probe_setup();dc_arena_jak_evidence.debug_calls=0;
  CHECK(!dc_arena_jak_finish(&rt) && dc_arena_jak_evidence.blocker==1 && !dc_arena_jak_evidence.continue_calls);
  printf("%u host assertions; original-call doubles only\n",checks);return 0;
}
