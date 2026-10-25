/* Same cache cases on host, no KOS/GPU emulation (AI-assisted), ISC. */
#include "render_cache_cases.h"
#include <stdio.h>
int main(int argc,char **argv) {
 dc_render_cache_report r;if(argc!=2)return 2;dc_render_cache_cases(argv[1],&r);
 printf("{\"total\":%u,\"passed\":%u,\"failed_line\":%u,\"windows\":%u,\"max_faces\":%u,\"peak_bytes\":%u}\n",
  (unsigned)r.total,(unsigned)r.passed,(unsigned)r.failed_line,(unsigned)r.windows,(unsigned)r.max_faces,(unsigned)r.peak_bytes);
 return r.failed_line?1:0;
}
