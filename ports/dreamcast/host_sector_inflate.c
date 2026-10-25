/* Original-free host decompression entry (AI-assisted), ISC. */
#include "sector_inflate_cases.h"
#include <stdio.h>
int main(void){dc_inflate_report r;dc_sector_inflate_cases(&r);
 printf("inflate %u/%u checks peakdecode=%u failedline=%u\n",(unsigned)r.passed,(unsigned)r.total,(unsigned)r.peak_decode_bytes,(unsigned)r.failed_line);
 return r.failed_line?1:0;}
