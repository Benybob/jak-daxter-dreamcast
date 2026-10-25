/* Native bounded render-window oracle (AI-assisted), ISC. */
#include <kos.h>
#include <stdio.h>
#include "render_cache_cases.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct{uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
dc_render_cache_report dc_cache_report;
int main(void) {
 dc_render_cache_report *r=&dc_cache_report;dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 dc_render_cache_cases("/rd",r);dc_backend.total=r->total;dc_backend.passed=r->passed;dc_backend.failed_case=r->failed_line;
 dc_backend.observed_lo=r->windows;dc_backend.observed_hi=r->max_faces;dc_backend.expected_lo=r->peak_bytes;
 snprintf((char *)dc_backend.description,80,"Render cache windows=%u maxfaces=%u peakbytes=%u",(unsigned)r->windows,(unsigned)r->max_faces,(unsigned)r->peak_bytes);
 dc_backend.state=r->failed_line?3:2;
 for(;;)thd_sleep(100);
 return 0;
}
