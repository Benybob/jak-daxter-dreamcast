/* Native boot of the unchanged original jak1 kernel startup (AI-assisted), ISC.
 * Native counterpart of the desktop C++ kernel's setup, then the original
 * top-level startups of all eight KERNEL.CGO files in their original load order.
 * With DC_KERNEL_DGO the same modules load from /rd/KERNEL.CGO, a DGO of DCSH objects. */
#include <kos.h>
#include <arch/cache.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "goal_module.h"
#include "goal_bridge.h"
#ifdef DC_KERNEL_DGO
#include "mips2c_dc.h"
#include "mips2c_cases.h"
#include "build/mips2c/mips2c_cases_expect.h"
#endif
#include "goal_kernel_types.h"
#include "goal_main_start.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#ifdef DC_KERNEL_DGO
#include "goal_dgo.h"
#include "goal_dgo_zst.h"
#include "build/game_cgo_order.h"
#include "build/vf_cases.h"
int dc_kernel_load_vf_fixture(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* First BAD_ARGUMENT site while GAME.CGO loads (dc_goal_error_site encoding). */
volatile uint32_t dc_boot_game_error_site;
/* The boot's environment for the mips2c differential cases: symbols a case
 * points into its region are restored after it. */
typedef struct {dc_goal_runtime *rt;unsigned count;uint32_t symbol[8],value[8];} dc_boot_m2c_saved;
static void boot_m2c_set_symbol(dc_m2c_env *e,const char *name,uint32_t value){
  dc_boot_m2c_saved *saved=e->context;uint32_t symbol=dc_goal_intern(saved->rt,name);unsigned i;
  for(i=0;i<saved->count && saved->symbol[i]!=symbol;++i){}
  if(i==saved->count && i<8){saved->symbol[i]=symbol;saved->value[i]=dc_goal_symbol_value(saved->rt,symbol);++saved->count;}
  dc_goal_set_symbol(saved->rt,symbol,value);
}
static uint64_t boot_m2c_call(dc_m2c_env *e,const char *name,const uint64_t *args){(void)e;return dc_mips2c_call(name,args);}
static uint64_t boot_m2c_call_jalr(dc_m2c_env *e,const char *name,const uint64_t *args){(void)e;return dc_mips2c_call_jalr(name,args);}
static void boot_m2c_restore(dc_boot_m2c_saved *saved){
  while(saved->count){--saved->count;dc_goal_set_symbol(saved->rt,saved->symbol[saved->count],saved->value[saved->count]);}
}
/* Diagnostics (verify_backend.py --dump): display-loop frames completed, and the
 * error and first refusal site where they stopped. */
volatile uint32_t dc_boot_display_frames,dc_boot_display_error,dc_boot_display_site;
/* Read by verify_backend.py --dump: arena use around every GAME.CGO object. */
struct dc_boot_memory_trace {uint32_t magic,objects,completed,failed,capacity,before,failed_request,after[DC_GOAL_DGO_MAX_OBJECTS];};
struct dc_boot_memory_trace dc_boot_memory_trace;
#else
#include "build/boot_gcommon.h"
#include "build/boot_gstring_h.h"
#include "build/boot_gkernel_h.h"
#include "build/boot_gkernel.h"
#include "build/boot_pskernel.h"
#include "build/boot_gstring.h"
#include "build/boot_dgo_h.h"
#include "build/boot_gstate.h"
int dc_kernel_boot_load_valid(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
#endif
static void sync_code(uint32_t allocation,uint32_t bytes,void *context){
  (void)context;dcache_flush_range(allocation,bytes);icache_flush_range(allocation,bytes);
}
static uint32_t entry(dc_goal_runtime *rt,const char *name){return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
/* Desktop kscheme/kmachine raise *enable-method-set* while the kernel and GAME
 * DGOs load, so a method defined on a parent after its child types exist (gkernel's
 * stack-size-set! on thread, after gkernel-h made cpu-thread) reaches the children. */
static void method_set_depth(dc_goal_runtime *rt,int32_t delta){
  enum dc_goal_error prior=rt->error;uint32_t symbol=dc_goal_intern(rt,"*enable-method-set*");
  dc_goal_set_symbol(rt,symbol,dc_goal_symbol_value(rt,symbol)+(uint32_t)delta);
  rt->error=prior;  /* intern resets the error; the load's result stays */
}
static uint32_t word(dc_goal_runtime *rt,uint32_t p,unsigned off){uint32_t v=0;const void *q=dc_goal_resolve(rt,p+off,4);if(q)memcpy(&v,q,4);return v;}
static uint32_t typed(dc_goal_runtime *rt,const char *global,const char *type_name){
  uint32_t value=entry(rt,global),type=entry(rt,type_name);
  return value && type && dc_goal_type_of(rt,value)==type;
}
uint32_t dc_kernel_boot_cases(void *memory,uint32_t capacity,dc_runtime_check check,void *context,
                              char *description,size_t description_bytes){
  dc_goal_runtime rt;dc_goal_module_result r;dc_kernel_types_result types;uint64_t value;
  uint32_t code_bytes=0,before;unsigned id=0;const char *text;uint32_t length;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++id,label,context)
#define STEP(call,label) do{int ok_=(call);CHECK(ok_?1:100+rt.error,1,label);if(!ok_)goto done;code_bytes+=r.code_bytes;memset(&r,0,sizeof r);}while(0)
  memset(&r,0,sizeof r);
  CHECK(dc_goal_init_native(&rt,memory,capacity),1,"native runtime for the original kernel boot");
  /* Native counterpart of the C++ kernel's scheme setup. */
  STEP(dc_goal_install_core_bridges(&rt,sync_code,&r),"core pair/type/call-error bridges");
  STEP(dc_goal_install_kernel_types(&rt,&types),"original fixed kernel types (process, thread, cpu-thread, ...)");
  STEP(dc_goal_install_type_bridges(&rt,sync_code,&r),"type-new, method-set! and basic-new bridges");
  STEP(dc_goal_install_integer_bridge(&rt,sync_code,&r),"integer fault bridge");
  STEP(dc_goal_install_object_bridge(&rt,sync_code,&r),"object allocation bridge");
  STEP(dc_goal_install_process_bridge(&rt,sync_code,&r),"authenticated current-process bridge");
  STEP(dc_goal_install_scope_bridge(&rt,sync_code,&r),"lexical process scope bridges");
  STEP(dc_goal_install_transfer_bridge(&rt,sync_code,&r),"native thread transfer primitives");
  STEP(dc_goal_install_main_start_guard(&rt,sync_code,&r),"set-to-run-bootstrap guard");
  STEP(dc_goal_install_catch_bridge(&rt,sync_code,&r),"native catch-frame and throw-dispatch");
  STEP(dc_goal_install_kernel_services(&rt,sync_code,&r),"_format, malloc and nothing services");
  STEP(dc_goal_install_pc_services(&rt,sync_code,&r),"PC-port services with desktop's no-display answers");
  CHECK(dc_goal_set_symbol(&rt,dc_goal_intern(&rt,"*debug-segment*"),rt.s7),1,"no debug segment, as a non-debug kernel");
  dc_goal_format_capture_clear();before=rt.used;
  method_set_depth(&rt,1);
#ifdef DC_KERNEL_DGO
  /* Native counterpart of the C++ kernel's load_and_link_dgo("KERNEL.CGO"). */
  {
    static dc_goal_dgo_result dgo;
    static const char *const order[]={"gcommon","dc-scalar-valid","gstring-h","gkernel-h","gkernel",
                                      "pskernel","gstring","dgo-h","gstate"};
    FILE *file=fopen("/rd/KERNEL.CGO","rb");int loaded;unsigned i;
    CHECK(file!=NULL,1,"KERNEL.CGO opens from the romdisk");if(!file)goto done;
    loaded=dc_goal_load_dgo(&rt,file,sync_code,NULL,&dgo);fclose(file);
    CHECK(loaded?1:100+rt.error,1,"every KERNEL.CGO object links and runs its original startup in order");
    CHECK(dgo.failed_object,UINT32_MAX,"no KERNEL.CGO object fails");
    CHECK(strcmp(dgo.name,"KERNEL.CGO"),0,"DGO header names KERNEL.CGO");
    CHECK(dgo.objects,9,"eight original objects plus the scalar valid? override");
    CHECK(dgo.completed,9,"all nine objects complete");
    CHECK(dgo.initializers,8,"each original object runs its top-level startup");
    for(i=0;i<9;++i)CHECK(strcmp(dgo.object_names[i],order[i]),0,"objects load in original KERNEL.CGO order");
    if(!loaded)goto done;
    code_bytes+=dgo.code_bytes;(void)value;
  }
#else
  /* The unchanged original kernel files, in their original KERNEL.CGO load order. */
#define BOOT(M,m,label) \
  STEP(dc_goal_load_module_data(&rt,goal_boot_##m##_exports,GOAL_BOOT_##M##_EXPORT_COUNT, \
       GOAL_BOOT_##M##_DATA,GOAL_BOOT_##M##_DATA_COUNT,sync_code,NULL,&r),"original " label " links"); \
  STEP(dc_goal_run_initializer(&rt,GOAL_BOOT_##M##_INITIALIZER,&value),"original " label " startup runs")
  BOOT(GCOMMON,gcommon,"gcommon");
  STEP(dc_kernel_boot_load_valid(&rt,sync_code,&r),"scalar-configured original valid? replaces desktop bounds");
  BOOT(GSTRING_H,gstring_h,"gstring-h");
  BOOT(GKERNEL_H,gkernel_h,"gkernel-h");
  BOOT(GKERNEL,gkernel,"gkernel (native primitives bound)");
  BOOT(PSKERNEL,pskernel,"pskernel");
  BOOT(GSTRING,gstring,"gstring");
  BOOT(DGO_H,dgo_h,"dgo-h");
  BOOT(GSTATE,gstate,"gstate");
#undef BOOT
#endif
  method_set_depth(&rt,-1);
  CHECK(rt.error,DC_GOAL_OK,"complete original kernel startup leaves no fault");
  STEP(dc_goal_install_backup_floor(&rt,sync_code,&r),"original stack-size-set! keeps the 512-byte native backup floor");
  /* What the original startup built. */
  CHECK(typed(&rt,"*kernel-context*","kernel-context"),1,"original kernel context");
  CHECK(word(&rt,entry(&rt,"*kernel-context*"),20),rt.s7,"no current process after boot");
  CHECK(entry(&rt,"*dram-stack*")!=0,1,"original DRAM execution stack");
  CHECK(typed(&rt,"*listener-process*","process"),1,"original listener process");
  CHECK(word(&rt,entry(&rt,"*listener-process*"),36),1,"listener process has PID 1");
  CHECK(typed(&rt,"*null-process*","process"),1,"original null process");
  CHECK(typed(&rt,"*16k-dead-pool*","dead-pool"),1,"16k fixed process pool");
  CHECK(typed(&rt,"*8k-dead-pool*","dead-pool"),1,"8k fixed process pool");
  CHECK(typed(&rt,"*4k-dead-pool*","dead-pool"),1,"4k fixed process pool");
  CHECK(typed(&rt,"*target-dead-pool*","dead-pool"),1,"target process pool");
  CHECK(typed(&rt,"*camera-dead-pool*","dead-pool"),1,"camera process pool");
  CHECK(typed(&rt,"*camera-master-dead-pool*","dead-pool"),1,"camera-master process pool");
  CHECK(typed(&rt,"*nk-dead-pool*","dead-pool-heap"),1,"shared actor process heap");
  CHECK(entry(&rt,"*default-dead-pool*"),entry(&rt,"*nk-dead-pool*"),"default pool is the actor heap");
  CHECK(entry(&rt,"*debug-dead-pool*")==0 || entry(&rt,"*debug-dead-pool*")==rt.s7,1,"no debug pool without a debug segment");
  CHECK(dc_goal_type_of(&rt,entry(&rt,"*dead-pool-list*")),rt.pair_type,"original dead-pool list");
  CHECK(typed(&rt,"*active-pool*","process-tree"),1,"original active process tree");
  length=dc_goal_format_capture(&text);
  CHECK(length>0 && strstr(text,"] vec4s")!=NULL,1,"original startup (inspect *test-vec4s*) prints its type symbol through native _format");
  CHECK(strstr(text,"x: 0.0000")!=NULL,1,"vec4s float field formatted from its variadic bit pattern");
#ifdef DC_KERNEL_DGO
  /* The compiled prefix of the original GAME.CGO loads on top of the kernel. */
  {
    static dc_goal_dgo_result game;
    static dc_goal_process_spans game_spans;static dc_goal_process_span game_rows[64];
    static dc_goal_pp_scope_stack game_scopes;
    FILE *file=dc_goal_open_zst("/rd/GAME.CGO.zst");int loaded;unsigned i;
    CHECK(file!=NULL,1,"zstd GAME.CGO prefix opens from the romdisk");
    /* Startups spawn processes (time-of-day): their scopes are authenticated. */
    CHECK(dc_goal_register_process_spans(&rt,&game_spans,game_rows,64),1,"ownership ledger registered for GAME.CGO startups");
    CHECK(dc_goal_register_pp_scope(&rt,&game_scopes),1,"lexical scope metadata registered for GAME.CGO startups");
    /* The PC port's mips2c functions built for SH-4 (build_mips2c.py): every
     * registration links, and GAME.CGO's lookups of them get native functions. */
    CHECK(dc_mips2c_install(&rt),94,"the 94 functions of the 24 built mips2c files link natively");
    /* Idle VRAM from 1 to 6 MiB (romdisk_stage.c) holds the PS2 DMA buffers. */
    CHECK(dc_goal_register_extension(&rt,(void *)0xa5100000u,0xa5100000u,5u*1024u*1024u),1,
          "idle VRAM registered as the GOAL extension region");
    CHECK(dc_goal_place_in_extension(&rt,"dma-buffer",512u*1024u),1,"large dma-buffers placed in VRAM");
    if(file){
      method_set_depth(&rt,1);
      loaded=dc_goal_load_dgo(&rt,file,sync_code,NULL,&game);fclose(file);
      method_set_depth(&rt,-1);
      dc_boot_game_error_site=dc_goal_error_site;  /* read by verify_backend.py --dump */
      dc_boot_memory_trace.objects=game.objects;dc_boot_memory_trace.completed=game.completed;
      dc_boot_memory_trace.failed=game.failed_object;dc_boot_memory_trace.capacity=capacity;
      dc_boot_memory_trace.before=game.arena_before;dc_boot_memory_trace.failed_request=dc_goal_last_failed_alloc;
      memcpy(dc_boot_memory_trace.after,game.arena_after,sizeof game.arena_after);
      dc_boot_memory_trace.magic=0x4d454d54u;  /* "TMEM" */
      CHECK(game.failed_object==UINT32_MAX?0u:1000u*game.failed_object+rt.error,0,
            "no GAME.CGO object fails (observed = 1000 * object index + error)");
      CHECK(loaded?1:100+rt.error,1,"GAME.CGO prefix links and runs each original startup in order");
      CHECK(game.objects,DC_GAME_PREFIX,"GAME.CGO holds the compiled prefix of original engine files");
      CHECK(game.initializers,DC_GAME_PREFIX,"each engine file runs its top-level startup");
      CHECK(dc_goal_mips2c_native(),76,"all 76 of GAME.CGO's mips2c lookups return native code");
      /* The differential cases (mips2c_cases.h) through the native SH-4 build,
       * compared with the host oracle: the same C++ as desktop runs it. */
      {dc_m2c_env env;dc_boot_m2c_saved saved;uint32_t region=dc_goal_alloc_temporary(&rt,DC_M2C_REGION+0x4000u);unsigned k;
       /* 16 KiB aligned like the PS2 scratchpad: translated code forms scratchpad
        * addresses with ori (render-ocean-quad: ori t4, spad, 12288). */
       if(region)region=(region+0x3fffu)&~0x3fffu;
       CHECK(region!=0 && DC_M2C_EXPECT_COUNT==DC_M2C_CASE_COUNT,1,"mips2c case region and oracle results");
       if(region && DC_M2C_EXPECT_COUNT==DC_M2C_CASE_COUNT){
         memset(&saved,0,sizeof saved);saved.rt=&rt;
         env.region=dc_goal_resolve(&rt,region,DC_M2C_REGION);env.base=region;env.s7=rt.s7;
         env.set_symbol=boot_m2c_set_symbol;env.call=boot_m2c_call;env.call_jalr=boot_m2c_call_jalr;env.context=&saved;
         for(k=0;k<DC_M2C_CASE_COUNT;++k){
           uint32_t result,crc=dc_m2c_run_case(&env,k,&result);
           boot_m2c_restore(&saved);
           CHECK(crc,dc_m2c_expect[k].crc,dc_m2c_expect[k].name);
           CHECK(result,dc_m2c_expect[k].result,"mips2c case result matches the host oracle");
         }
         CHECK(dc_mips2c_jalr_calls()>=3,1,"jalr onto a mips2c stub goes through the generic entry (GPR bits, as desktop)");
         CHECK(rt.error,DC_GOAL_OK,"native mips2c calls leave no runtime fault");
       }
       dc_goal_release_temporary(&rt);rt.error=DC_GOAL_OK;}
      {uint32_t placed_bytes,placed=dc_goal_extension_placed(&placed_bytes);
       CHECK(placed,2,"display's two global DMA buffers live in VRAM");
       CHECK(placed_bytes>=2u*1712u*1024u && rt.extension_used>=placed_bytes,1,"they take their 1,712 KiB each there");}
      for(i=0;i<DC_GAME_PREFIX;++i)CHECK(strcmp(game.object_names[i],dc_game_order[i]),0,"engine objects in original GAME.CGO order");
      CHECK(entry(&rt,"rand-vu-init")!=0 && entry(&rt,"truncate")!=0,1,"original math functions are installed");
      code_bytes+=game.code_bytes;rt.error=DC_GOAL_OK;
    }
    CHECK(dc_goal_pp_scope_depth(&rt),0,"every startup scope was left");
    /* time-of-day's startup spawned *time-of-day-proc* through the original
     * run-function-in-process: init ran to its (go), so it waits to run. */
    {uint32_t tod=word(&rt,entry(&rt,"*time-of-day-proc*"),0),waiting=dc_goal_find(&rt,"waiting-to-run");
     CHECK(word(&rt,tod,32),waiting,"spawned time-of-day process waits to run after its init");rt.error=DC_GOAL_OK;}
    CHECK(dc_goal_pp_scope_detach(&game_scopes,&rt),1,"quiescent GAME.CGO scope metadata detaches");
    CHECK(dc_goal_process_spans_detach(&rt),1,"GAME.CGO ownership ledger detaches");
  }
  /* VU0 vf lowering: the fixture forms and original vector-h functions, every
   * lane compared bit for bit with independently computed expectations. */
  {
    typedef uint64_t (*vf4_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
    typedef uint64_t (*vf3_fn)(uint64_t,uint64_t,uint64_t);
    typedef uint64_t (*vf1_fn)(uint64_t);
    typedef float (*dot_fn)(uint64_t,uint64_t);
    typedef float (*lane_fn)(uint64_t);
    uint32_t vectors=dc_goal_alloc(&rt,64),i,passed=0;
    CHECK(vectors!=0 && !(vectors&15u),1,"aligned vector scratch for the vf checks");
    STEP(dc_kernel_load_vf_fixture(&rt,sync_code,&r),"vf fixture forms link");
    for(i=0;i<DC_VF_CASES;++i){
      uint32_t dst=vectors,a=vectors+16,b=vectors+32,c=vectors+48,fn=entry(&rt,dc_vf_cases[i].name),got[4]={0,0,0,0};
      float value;
      memcpy(dc_goal_resolve(&rt,a,48),dc_vf_inputs[dc_vf_cases[i].set],48);
      memset(dc_goal_resolve(&rt,dst,16),0xa5,16);
      if(!fn){CHECK(0,1,dc_vf_cases[i].name);continue;}
      switch(dc_vf_cases[i].kind){
        case 0: ((vf4_fn)(uintptr_t)fn)(dst,a,b,c);memcpy(got,dc_goal_resolve(&rt,dst,16),16);break;
        case 1: value=((dot_fn)(uintptr_t)fn)(a,b);memcpy(got,&value,4);break;
        case 2: ((vf3_fn)(uintptr_t)fn)(dst,a,b);memcpy(got,dc_goal_resolve(&rt,dst,16),16);break;
        case 3: ((vf1_fn)(uintptr_t)fn)(dst);memcpy(got,dc_goal_resolve(&rt,dst,16),16);break;
        default: value=((lane_fn)(uintptr_t)fn)(a);memcpy(got,&value,4);break;
      }
      {int same=!memcmp(got,dc_vf_cases[i].expect,16);passed+=same;
       if(!same){static char why[120];snprintf(why,sizeof why,"%s set %u: %08lx %08lx %08lx %08lx",dc_vf_cases[i].name,
                 (unsigned)dc_vf_cases[i].set,(unsigned long)got[0],(unsigned long)got[1],(unsigned long)got[2],(unsigned long)got[3]);
                 CHECK(0,1,why);}}
    }
    CHECK(passed,DC_VF_CASES,"every vf case matches its independent expectation bit for bit");
    CHECK(rt.error,DC_GOAL_OK,"vf checks leave no runtime fault");
  }
#endif  /* A process started the original way (process-spawn-function: get-process,
   * activate, run-next-time-in-process) runs under the original kernel-dispatcher. */
  {
    typedef uint64_t (*zero_fn)(void);typedef uint64_t (*one_fn)(uint64_t);
    typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);typedef uint64_t (*four_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
    typedef uint64_t (*eight_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
    enum {STATUS=32,PID=36,MAIN=40,TOP=44,HEAP_TOP=80,RESUME_HOOK=16,NEXT_PID=12,SELF=24,CHILD=16,GET_PROCESS=14,ACTIVATE=9,DEACTIVATE=10,
          STACK_BYTES=16384,FRAMES=3};
    static dc_goal_process_spans table;static dc_goal_process_span rows[32];static dc_goal_pp_scope_stack scopes;
    static dc_goal_fiber fiber;
    uint32_t pool=entry(&rt,"*8k-dead-pool*"),parent=entry(&rt,"*default-pool*"),process_type=entry(&rt,"process");
    uint32_t name=dc_goal_intern(&rt,"dc-boot-spawn"),suspended=dc_goal_intern(&rt,"suspended");
    uint32_t proc,thread,stack,bytes,pid,i;
    zero_fn dispatch=(zero_fn)(uintptr_t)entry(&rt,"kernel-dispatcher");
    CHECK(dispatch!=NULL && entry(&rt,"looping-code")!=0,1,"original kernel-dispatcher and looping-code booted");
    CHECK(dc_goal_register_process_spans(&rt,&table,rows,32),1,"ownership ledger registered after boot");
    CHECK(dc_goal_register_pp_scope(&rt,&scopes),1,"lexical scope metadata registered after boot");
    /* Original post hooks run on *kernel-dram-stack*: the 32 KiB *dram-stack*
     * array's data, which starts 12 bytes after its tagged pointer. */
    {uint32_t dram=entry(&rt,"*dram-stack*"),base=(dram+12u+15u)&~15u,top=(dram+0x8000u)&~15u;
     CHECK(dc_goal_register_kernel_stack(&rt,base,top-base),1,"original *dram-stack* data registered for post hooks");}
    proc=(uint32_t)((three_fn)(uintptr_t)dc_goal_get_method(&rt,dc_goal_type_of(&rt,pool),GET_PROCESS))(pool,process_type,1024);
    CHECK(proc!=0 && proc!=rt.s7 && dc_goal_type_of(&rt,proc)==process_type,1,"original get-process takes a member of the 8k pool");
    if(!proc || proc==rt.s7)goto spawn_done;
    bytes=word(&rt,proc,HEAP_TOP)-(proc-4u);  /* raw allocation start to heap-top */
    CHECK(dc_goal_note_process_span(&rt,proc,bytes,NULL),1,"trusted C notes the original pool member's exact extent");
    stack=dc_goal_alloc(&rt,STACK_BYTES);CHECK(stack!=0 && !(stack&15u),1,"owned aligned execution stack");if(!stack)goto spawn_done;
    pid=word(&rt,entry(&rt,"*kernel-context*"),NEXT_PID);
    ((four_fn)(uintptr_t)dc_goal_get_method(&rt,process_type,ACTIVATE))(proc,parent,name,stack+STACK_BYTES);
    CHECK(rt.error,DC_GOAL_OK,"original activate completes");
    thread=word(&rt,proc,MAIN);
    CHECK(thread!=0 && thread!=rt.s7 && thread>proc && thread<proc+bytes,1,"original activate builds the main thread in the process heap");
    CHECK(word(&rt,proc,PID),pid,"original activate assigns the next kernel PID");
    CHECK(word(&rt,proc,8)!=rt.s7,1,"original change-parent moves the process under *default-pool*");
    ((eight_fn)(uintptr_t)entry(&rt,"set-to-run"))(thread,entry(&rt,"looping-code"),0,0,0,0,0,0);
    CHECK(word(&rt,proc,STATUS),dc_goal_find(&rt,"waiting-to-run"),"original run-next-time-in-process arms the main thread");
    {int imported=dc_goal_fiber_init_main(&fiber,&rt,proc,bytes,thread,stack,STACK_BYTES);
     CHECK(imported?1:100+rt.error,1,"armed original image imports into a native fiber");
     if(!imported){rt.error=DC_GOAL_OK;goto spawn_done;}}
    CHECK(word(&rt,thread,RESUME_HOOK),entry(&rt,"dc-c-thread-resume"),"original constructor installed the native thread-resume hook");
    CHECK(word(&rt,thread,32),512,"original activate reserves the Dreamcast main-thread backup");
    CHECK(dc_goal_register_thread_fiber(&rt,&fiber),1,"fiber registered for the original resume-hook");
    for(i=0;i<FRAMES;++i){
      dispatch();
      CHECK(rt.error,DC_GOAL_OK,"original kernel-dispatcher frame completes without error");
      CHECK(word(&rt,proc,STATUS),suspended,"the spawned process runs and suspends each frame");
      CHECK(fiber.phase,DC_FIBER_SUSPENDED,"its native fiber is suspended between frames");
      CHECK(word(&rt,entry(&rt,"*kernel-context*"),20),rt.s7,"dispatcher clears current-process after the frame");
    }
#ifdef DC_KERNEL_DGO
    /* The process time-of-day's startup spawned ran too: the dispatcher gave it a
     * native fiber on its first resume, and its state code suspends each frame. */
    {uint32_t tod=word(&rt,entry(&rt,"*time-of-day-proc*"),0);
     CHECK(word(&rt,tod,32),suspended,"spawned time-of-day process runs and suspends under the dispatcher");}
    /* The original main loop, headless: display-loop started as main.gc's `on`
     * does (process-spawn-function display-loop :name 'display :from
     * *4k-dead-pool* :to *display-pool*, on the default fake-scratchpad stack),
     * with *run* set. No level is loaded and nothing draws; each dispatcher frame
     * runs one iteration of the loop and suspends. */
    {enum {DISPLAY_FRAMES=60,REAL_INTEGRAL_FRAMES=804};  /* display field at 808, less the basic tag */
     uint32_t dpool=entry(&rt,"*4k-dead-pool*"),dparent=entry(&rt,"*display-pool*");
     uint32_t scratch=entry(&rt,"*fake-scratchpad-stack*"),dname=dc_goal_intern(&rt,"display"),dproc,dthread;
     unsigned frame,completed=0;uint32_t before;
     CHECK(dpool && dparent && scratch && entry(&rt,"display-loop"),1,"display-loop and its pools booted");
     dproc=(uint32_t)((three_fn)(uintptr_t)dc_goal_get_method(&rt,dc_goal_type_of(&rt,dpool),GET_PROCESS))(dpool,process_type,0x4000);
     CHECK(dproc!=0 && dproc!=rt.s7,1,"original get-process takes a *4k-dead-pool* member for display");
     if(dproc && dproc!=rt.s7){
       ((four_fn)(uintptr_t)dc_goal_get_method(&rt,process_type,ACTIVATE))(dproc,dparent,dname,scratch+32u*1024u);
       dthread=word(&rt,dproc,MAIN);
       ((eight_fn)(uintptr_t)entry(&rt,"set-to-run"))(dthread,entry(&rt,"display-loop"),0,0,0,0,0,0);
       CHECK(rt.error,DC_GOAL_OK,"display process activates and arms display-loop");
       dc_goal_set_symbol(&rt,dc_goal_find(&rt,"*run*"),dc_goal_find(&rt,"#t"));
       dc_goal_set_symbol(&rt,dc_goal_find(&rt,"*dproc*"),dproc);
       before=word(&rt,entry(&rt,"*display*"),REAL_INTEGRAL_FRAMES);
       for(frame=0;frame<DISPLAY_FRAMES && rt.error==DC_GOAL_OK;++frame){
         dc_goal_error_site=0;
         dispatch();
         if(rt.error==DC_GOAL_OK && word(&rt,dproc,STATUS)==suspended)++completed;
         else {dc_boot_display_error=rt.error|((uint32_t)dc_goal_sticky_error<<8);dc_boot_display_site=dc_goal_error_site;}
       }
       dc_boot_display_frames=completed;
       CHECK(completed,DISPLAY_FRAMES,"original display-loop runs and suspends each frame, headless");
       CHECK(rt.error,DC_GOAL_OK,"display-loop frames leave no runtime fault");
       /* Every swap-display's display-frame-start counts at least one frame. */
       CHECK(word(&rt,entry(&rt,"*display*"),REAL_INTEGRAL_FRAMES)-before>=DISPLAY_FRAMES,1,
             "*display* real-integral-frame-counter advances every frame");
       rt.error=DC_GOAL_OK;
     }}
#endif
    CHECK(dc_goal_unregister_thread_fiber(&rt,&fiber),1,"fiber unregistered");
    {uint32_t listener=entry(&rt,"*listener-process*"),lbytes=word(&rt,listener,HEAP_TOP)-(listener-4u);
     CHECK(dc_goal_note_process_span(&rt,listener,lbytes,NULL),1,"trusted C notes the original listener's exact extent");
     CHECK(dc_goal_bind_process_context(&rt,listener,lbytes),1,"original listener bound as the caller that kills it");}
    ((one_fn)(uintptr_t)dc_goal_get_method(&rt,process_type,DEACTIVATE))(proc);
    CHECK(rt.error,DC_GOAL_OK,"original deactivate completes");dc_goal_clear_process_context(&rt);
    CHECK(word(&rt,proc,STATUS),dc_goal_find(&rt,"dead"),"original deactivate kills the spawned process");
    CHECK(word(&rt,pool,CHILD),proc+SELF,"original deactivate returns the process to the 8k pool");
    dispatch();
    CHECK(rt.error,DC_GOAL_OK,"dispatcher frame after the process died succeeds");
    CHECK(dc_goal_fiber_clear(&fiber),1,"dead process's fiber clears");
    CHECK(dc_goal_pp_scope_detach(&scopes,&rt),1,"quiescent scope metadata detaches");
spawn_done:
    rt.error=DC_GOAL_OK;dc_goal_clear_process_context(&rt);
    CHECK(dc_goal_process_spans_detach(&rt),1,"ownership ledger detaches");
  }
#ifdef DC_KERNEL_DGO
  /* Damaged containers refuse before linking anything. */
  {
    /* The image buffer lives only for these tests (malloc, not 400 KB of bss). */
    enum {IMAGE_BYTES=400000};
    static dc_goal_dgo_result bad;unsigned char *image=malloc(IMAGE_BYTES);
    FILE *file=image?fopen("/rd/KERNEL.CGO","rb"):NULL;size_t n=file?fread(image,1,IMAGE_BYTES,file):0;uint32_t symbols=rt.symbol_count,used=rt.used;
    if(file){fclose(file);}
    CHECK(n>64+64+64 && n<IMAGE_BYTES,1,"KERNEL.CGO read back for damage tests");
    if(image){
    memcpy(image+0,"oZlB",4);file=fopen("/ram/bad.cgo","wb");if(file){fwrite(image,1,n,file);fclose(file);}
    file=fopen("/ram/bad.cgo","rb");
    CHECK(file && !dc_goal_load_dgo(&rt,file,sync_code,NULL,&bad) && bad.failed_object==UINT32_MAX,1,"compressed DGO header refuses");
    if(file){fclose(file);}rt.error=DC_GOAL_OK;
    image[0]=9;image[1]=image[2]=image[3]=0;image[64+64+100]^=0x5a;  /* inside the first DCSH object */
    file=fopen("/ram/bad.cgo","wb");if(file){fwrite(image,1,n,file);fclose(file);}
    file=fopen("/ram/bad.cgo","rb");
    CHECK(file && !dc_goal_load_dgo(&rt,file,sync_code,NULL,&bad) && bad.failed_object==0 && bad.completed==0,1,
          "corrupted first object refuses through its CRC");
    if(file){fclose(file);}rt.error=DC_GOAL_OK;unlink("/ram/bad.cgo");}
    free(image);
    CHECK(rt.symbol_count==symbols && rt.used==used,1,"refused DGOs link nothing");
  }
#endif
  snprintf(description,description_bytes,"BOOT GOAL=%lu arena=%lu/%lu startup=%lu symbols=%lu",
           (unsigned long)code_bytes,(unsigned long)rt.used,(unsigned long)capacity,
           (unsigned long)(rt.used-before),(unsigned long)rt.symbol_count);
done:
#undef CHECK
#undef STEP
  return code_bytes;
}
