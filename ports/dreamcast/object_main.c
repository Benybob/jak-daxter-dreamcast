/* Stock-RAM SH-4 data-object diagnostic (AI-assisted), ISC. */
#include <kos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "object_cases.h"
#include "object_load_cases.h"
#include "goal_kernel_types.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {
  uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
  uint32_t observed_lo,observed_hi,expected_lo,expected_hi;
  char description[80];
} object_telemetry;
typedef char object_telemetry_size[sizeof(object_telemetry)==128?1:-1];
volatile object_telemetry dc_backend={.magic=0x34485347u,.version=1};
static void check(uint64_t a,uint64_t b,unsigned id,const char *name,void *context) {
  unsigned i; (void)context; ++dc_backend.total;
  if (a==b) { ++dc_backend.passed; return; }
  if (dc_backend.failed_case) return;
  dc_backend.failed_case=id;
  dc_backend.observed_lo=(uint32_t)a; dc_backend.observed_hi=(uint32_t)(a>>32);
  dc_backend.expected_lo=(uint32_t)b; dc_backend.expected_hi=(uint32_t)(b>>32);
  for (i=0;i<79 && name[i];++i) dc_backend.description[i]=name[i];
  dc_backend.description[i]=0;
}
int main(void) {
  unsigned id=0;
  dc_goal_runtime rt;
  dc_kernel_types_result types;
  void *memory;
  dc_backend.state=1; dc_backend.system_ram=_arch_mem_top-0x8c000000u;
  dc_object_cases(check,NULL,&id);
  memory=memalign(32,1048576);
  check(memory!=NULL,1,++id,"data-loader arena allocation",NULL);
  if (memory) {
    int initialized=dc_goal_init_native(&rt,memory,1048576);
    check(initialized,1,++id,"native data-loader P0 runtime",NULL);
    if (initialized) {
      int installed=dc_goal_install_kernel_types(&rt,&types);
      check(installed,1,++id,"original fixed kernel metadata before assets",NULL);
      check(types.types,35,++id,"all original fixed type definitions before assets",NULL);
      if (installed) dc_object_load_cases(&rt,check,NULL,&id);
    }
    if (!dc_backend.failed_case) snprintf((char*)dc_backend.description,80,"native V4 data linked; arena=%lu symbols=%lu",
                                         (unsigned long)rt.used,(unsigned long)rt.symbol_count);
    free(memory);
  }
  dc_backend.state=dc_backend.failed_case?3:2;
  thd_sleep(500); return dc_backend.failed_case?1:0;
}
