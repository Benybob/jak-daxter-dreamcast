/* Native capsule/controller and optional original patch oracle (AI-assisted), ISC. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
unsigned dc_world_collision_cases(unsigned *);
unsigned dc_world_player_cases(unsigned *);
unsigned dc_world_patch_cases(FILE *,unsigned *);
extern uint32_t dc_world_patch_max_tests;
int main(void){unsigned n,count,bad;
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 bad=dc_world_collision_cases(&count);bad+=dc_world_player_cases(&n);count+=n;
#ifdef DC_WORLD_REAL_PATCH
 {FILE *f=fopen("/rd/world.dcc","rb");if(!f){++bad;++count;}
  else{bad+=dc_world_patch_cases(f,&n);count+=n;fclose(f);}}
 snprintf((char *)dc_backend.description,80,"Original 576tri patch + capsule/controller max-distance-tests=%lu",(unsigned long)dc_world_patch_max_tests);
#else
 snprintf((char *)dc_backend.description,80,"Synthetic capsule/controller/PAT oracle; not original GOAL collision");
#endif
 dc_backend.total=count;dc_backend.passed=count-bad;dc_backend.failed_case=bad?1:0;dc_backend.state=bad?3:2;
 for(;;)thd_sleep(100);
 return bad?1:0;
}
