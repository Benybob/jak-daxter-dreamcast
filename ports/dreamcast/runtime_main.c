/* Native SH-4 runtime acceptance runner (AI-assisted), ISC. */
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime_cases.h"
#ifdef DC_RUNTIME_GOAL
uint32_t dc_runtime_goal_cases(void *,uint32_t,dc_runtime_check,void *,char *,size_t);
#endif
#ifdef DC_KERNEL_BOOT
uint32_t dc_kernel_boot_cases(void *,uint32_t,dc_runtime_check,void *,char *,size_t);
/* romdisk_stage.c (DGO boot only) moves the romdisk to VRAM and sizes the arena. */
int __attribute__((weak)) dc_stage_romdisk(void) { return 1; }
uint32_t __attribute__((weak)) dc_kernel_arena_bytes(void) { return 12u*1024u*1024u; }
#endif

KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {
  uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
  uint32_t observed_lo,observed_hi,expected_lo,expected_hi;
  char description[80];
} runtime_telemetry;
typedef char runtime_telemetry_layout[sizeof(runtime_telemetry)==128?1:-1];
volatile runtime_telemetry dc_backend={.magic=0x34485347u,.version=1};

static void check(uint64_t actual,uint64_t expected,unsigned which,const char *name,void *unused) {
  unsigned i;
  (void)unused;
  ++dc_backend.total;
  if (actual==expected) { ++dc_backend.passed; return; }
  if (dc_backend.failed_case) return;
  dc_backend.failed_case=which;
  dc_backend.observed_lo=(uint32_t)actual; dc_backend.observed_hi=(uint32_t)(actual>>32);
  dc_backend.expected_lo=(uint32_t)expected; dc_backend.expected_hi=(uint32_t)(expected>>32);
  for (i=0;i<sizeof(dc_backend.description)-1 && name[i];++i) dc_backend.description[i]=name[i];
  dc_backend.description[i]=0;
}
int main(void) {
#ifdef DC_KERNEL_BOOT
  /* The original kernel startup builds its pools, stacks and listener here;
   * GAME.CGO's engine files load into the same arena. */
  const int staged=dc_stage_romdisk();  /* before any large allocation */
  const uint32_t capacity=dc_kernel_arena_bytes();
#else
  const uint32_t capacity=1024u*1024u;
#endif
  unsigned char *allocation=memalign(32,capacity+32);
  dc_runtime_stats stats;
  dc_backend.state=1; dc_backend.system_ram=_arch_mem_top-0x8c000000u;
  if (!allocation) {
    check(0,1,1000,"runtime arena allocation",NULL); dc_backend.state=3; return 1;
  }
#ifdef DC_KERNEL_BOOT
  if (!staged) {
    check(0,1,1001,"romdisk staged into VRAM",NULL); dc_backend.state=3; return 1;
  }
#endif
#ifndef DC_KERNEL_BOOT
  dc_runtime_cases(allocation+16,capacity,(uint32_t)(uintptr_t)(allocation+16),check,NULL,&stats);
#else
  memset(&stats,0,sizeof stats);
#endif
  if (!dc_backend.failed_case) {
    char message[80];
#ifdef DC_KERNEL_BOOT
    dc_backend.code_bytes=dc_kernel_boot_cases(allocation+16,capacity,check,NULL,message,sizeof(message));
    if (!dc_backend.failed_case) memcpy((void*)dc_backend.description,message,strlen(message)+1);
#elif defined(DC_RUNTIME_GOAL)
    dc_backend.code_bytes=dc_runtime_goal_cases(allocation+16,capacity,check,NULL,message,sizeof(message));
    if (!dc_backend.failed_case) memcpy((void*)dc_backend.description,message,strlen(message)+1);
#else
    snprintf(message,sizeof(message),"boot=%lu live=%lu arena=%lu symbols=%lu",
             (unsigned long)stats.boot_bytes,(unsigned long)stats.live_before_stress,
             (unsigned long)stats.arena_bytes,(unsigned long)stats.symbols);
    memcpy((void*)dc_backend.description,message,strlen(message)+1);
#endif
  }
  free(allocation);
  dc_backend.state=dc_backend.failed_case?3:2;
  printf("GOAL_RUNTIME %lu/%lu checks: %s\n",(unsigned long)dc_backend.passed,
         (unsigned long)dc_backend.total,(const char*)dc_backend.description);
  thd_sleep(500);
  return dc_backend.failed_case?1:0;
}
