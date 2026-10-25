/* Shared host/native cache oracle entry (AI-assisted), ISC. */
#include "sector_cache_cases.h"
#include <stdio.h>
int main(int argc,char **argv){dc_sector_oracle_result r;if(argc!=2)return 2;
 dc_sector_cache_oracle(argv[1],&r);printf("sector-cache %u/%u checks windows=%u max_faces=%u peak=%u failed_line=%u\n",r.passed,r.total,r.windows,r.max_faces,r.peak_geometry_bytes,r.failed_case);
 return r.failed_case?1:0;}
