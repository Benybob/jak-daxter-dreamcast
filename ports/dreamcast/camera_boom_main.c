/* Native camera query/rig and optional original-world oracle (AI-assisted), ISC. */
#include <kos.h>
#include <stdint.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct{uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
unsigned dc_camera_boom_cases(unsigned *);
unsigned dc_camera_rig_cases(unsigned *);
unsigned dc_camera_patch_cases(FILE *,unsigned *);
extern unsigned dc_camera_failed_line,dc_camera_patch_shortened;
int main(void){
 unsigned count,n,bad;dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 bad=dc_camera_boom_cases(&count);bad+=dc_camera_rig_cases(&n);count+=n;
#ifdef DC_CAMERA_REAL_PATCH
 {FILE *f=fopen("/rd/world.dcc","rb");if(!f){++bad;++count;}
  else{bad+=dc_camera_patch_cases(f,&n);count+=n;fclose(f);}}
#endif
 dc_backend.total=count;dc_backend.passed=count-bad;dc_backend.failed_case=dc_camera_failed_line;
 snprintf((char *)dc_backend.description,80,"Camera query/rig: failedline=%u originalshortened=%u",dc_camera_failed_line,dc_camera_patch_shortened);
 dc_backend.state=bad?3:2;for(;;)thd_sleep(100);return bad?1:0;
}
