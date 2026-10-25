/* Native PVR catalog upload/readback/rollback oracle (AI-assisted), ISC. */
#include <kos.h>
#include <stdio.h>
#include <string.h>
#include "render_gpu_cases.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct{uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
int main(void) {
 dc_render_catalog c={0};dc_gpu_report r={0};FILE *f;const char *error=NULL;
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,0},.vertex_buf_size=512*1024,.opb_overflow_count=3};
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 f=fopen("/rd/manifest.drm","rb");if(!f)error="catalog open";
 if(f){error=dc_render_catalog_load(f,&c);if(fclose(f)&&!error)error="catalog close";}
 if(!error&&pvr_init(&params)<0)error="PVR init";
 if(error){dc_backend.total=1;dc_backend.failed_case=1;snprintf((char *)dc_backend.description,80,"%s",error);}
 else{
  dc_render_gpu_cases(&c,&r);dc_backend.total=r.total;dc_backend.passed=r.passed;dc_backend.failed_case=r.failed_line;
  dc_backend.observed_lo=r.colors;dc_backend.observed_hi=r.bytes;dc_backend.expected_lo=r.peak_vram;dc_backend.expected_hi=r.peak_cpu;
  snprintf((char *)dc_backend.description,80,"Resident PVR colors=%u bytes=%u peakVRAM=%u CPU=%u",(unsigned)r.colors,(unsigned)r.bytes,(unsigned)r.peak_vram,(unsigned)r.peak_cpu);
  pvr_wait_ready();pvr_shutdown();
 }
 dc_render_catalog_free(&c);dc_backend.state=dc_backend.failed_case?3:2;
 for(;;)thd_sleep(100);
 return 0;
}
