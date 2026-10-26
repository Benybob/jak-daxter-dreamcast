/* Native boot of the unchanged original jak1 kernel startup (AI-assisted), ISC.
 * Native counterpart of the desktop C++ kernel's setup, then the original
 * top-level startups of all eight KERNEL.CGO files in their original load order. */
#include <kos.h>
#include <arch/cache.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "goal_module.h"
#include "goal_bridge.h"
#include "goal_kernel_types.h"
#include "goal_main_start.h"
#include "runtime_cases.h"
#include "build/boot_gcommon.h"
#include "build/boot_gstring_h.h"
#include "build/boot_gkernel_h.h"
#include "build/boot_gkernel.h"
#include "build/boot_pskernel.h"
#include "build/boot_gstring.h"
#include "build/boot_dgo_h.h"
#include "build/boot_gstate.h"
int dc_kernel_boot_load_valid(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
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
  snprintf(description,description_bytes,"BOOT GOAL=%lu arena=%lu/%lu startup=%lu symbols=%lu",
           (unsigned long)code_bytes,(unsigned long)rt.used,(unsigned long)capacity,
           (unsigned long)(rt.used-before),(unsigned long)rt.symbol_count);
done:
#undef CHECK
#undef STEP
  return code_bytes;
}
