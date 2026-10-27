/* Native boot of the unchanged original jak1 kernel startup (AI-assisted), ISC.
 * Native counterpart of the desktop C++ kernel's setup, then the original
 * top-level startups of all eight KERNEL.CGO files in their original load order.
 * With DC_KERNEL_DGO the same modules load from /rd/KERNEL.CGO, a DGO of DCSH objects. */
#include <kos.h>
#include <arch/cache.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "goal_module.h"
#include "goal_bridge.h"
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
  CHECK(dc_goal_set_symbol(&rt,dc_goal_intern(&rt,"*debug-segment*"),rt.s7),1,"no debug segment, as a non-debug kernel");
  dc_goal_format_capture_clear();before=rt.used;
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
  CHECK(rt.error,DC_GOAL_OK,"complete original kernel startup leaves no fault");
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
    FILE *file=dc_goal_open_zst("/rd/GAME.CGO.zst");int loaded;unsigned i;
    CHECK(file!=NULL,1,"zstd GAME.CGO prefix opens from the romdisk");
    if(file){
      loaded=dc_goal_load_dgo(&rt,file,sync_code,NULL,&game);fclose(file);
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
      for(i=0;i<DC_GAME_PREFIX;++i)CHECK(strcmp(game.object_names[i],dc_game_order[i]),0,"engine objects in original GAME.CGO order");
      CHECK(entry(&rt,"rand-vu-init")!=0 && entry(&rt,"truncate")!=0,1,"original math functions are installed");
      code_bytes+=game.code_bytes;rt.error=DC_GOAL_OK;
    }
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
    static dc_goal_process_spans table;static dc_goal_process_span rows[8];static dc_goal_pp_scope_stack scopes;
    static dc_goal_fiber fiber;
    uint32_t pool=entry(&rt,"*8k-dead-pool*"),parent=entry(&rt,"*default-pool*"),process_type=entry(&rt,"process");
    uint32_t name=dc_goal_intern(&rt,"dc-boot-spawn"),suspended=dc_goal_intern(&rt,"suspended");
    uint32_t proc,thread,stack,bytes,pid,i;
    zero_fn dispatch=(zero_fn)(uintptr_t)entry(&rt,"kernel-dispatcher");
    CHECK(dispatch!=NULL && entry(&rt,"looping-code")!=0,1,"original kernel-dispatcher and looping-code booted");
    CHECK(dc_goal_register_process_spans(&rt,&table,rows,8),1,"ownership ledger registered after boot");
    CHECK(dc_goal_register_pp_scope(&rt,&scopes),1,"lexical scope metadata registered after boot");
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
    static dc_goal_dgo_result bad;static unsigned char image[400000];
    FILE *file=fopen("/rd/KERNEL.CGO","rb");size_t n=file?fread(image,1,sizeof image,file):0;uint32_t symbols=rt.symbol_count,used=rt.used;
    if(file){fclose(file);}
    CHECK(n>64+64+64 && n<sizeof image,1,"KERNEL.CGO read back for damage tests");
    memcpy(image+0,"oZlB",4);file=fopen("/ram/bad.cgo","wb");if(file){fwrite(image,1,n,file);fclose(file);}
    file=fopen("/ram/bad.cgo","rb");
    CHECK(file && !dc_goal_load_dgo(&rt,file,sync_code,NULL,&bad) && bad.failed_object==UINT32_MAX,1,"compressed DGO header refuses");
    if(file){fclose(file);}rt.error=DC_GOAL_OK;
    image[0]=9;image[1]=image[2]=image[3]=0;image[64+64+100]^=0x5a;  /* inside the first DCSH object */
    file=fopen("/ram/bad.cgo","wb");if(file){fwrite(image,1,n,file);fclose(file);}
    file=fopen("/ram/bad.cgo","rb");
    CHECK(file && !dc_goal_load_dgo(&rt,file,sync_code,NULL,&bad) && bad.failed_object==0 && bad.completed==0,1,
          "corrupted first object refuses through its CRC");
    if(file){fclose(file);}rt.error=DC_GOAL_OK;unlink("/ram/bad.cgo");
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
