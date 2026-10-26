/* Host/native shared independent viewport oracle (AI-assisted), ISC. */
#include "render_frustum_cases.h"
#include <stdio.h>
int main(void) {dc_frustum_report r;dc_render_frustum_cases(&r);
 printf("render frustum %u/%u checks, cases=%u maxvertices=%u failedline=%u\n",r.passed,r.total,r.cases,r.max_vertices,r.failed_line);
 return r.failed_line?1:0;
}
