/* Native private/synthetic sector IO oracle (AI-assisted), ISC. */
#include <kos.h>
#include <stdio.h>
#include "sector_cache_cases.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct{uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
int main(void){dc_sector_oracle_result r;dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 dc_sector_cache_oracle("/rd",&r);dc_backend.total=r.total;dc_backend.passed=r.passed;dc_backend.failed_case=r.failed_case;
 dc_backend.expected_lo=r.peak_geometry_bytes;dc_backend.observed_lo=r.windows;dc_backend.observed_hi=r.max_faces;
 snprintf((char *)dc_backend.description,80,"Sector cache windows=%u maxfaces=%u peakgeometry=%u",(unsigned)r.windows,(unsigned)r.max_faces,(unsigned)r.peak_geometry_bytes);
 dc_backend.state=r.failed_case?3:2;for(;;)thd_sleep(100);return r.failed_case?1:0;}
