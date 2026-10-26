/* Shared independent viewport oracle (AI-assisted), ISC. */
#ifndef DC_RENDER_FRUSTUM_CASES_H
#define DC_RENDER_FRUSTUM_CASES_H
#include <stdint.h>
typedef struct {uint32_t total,passed,failed_line,cases,max_vertices;} dc_frustum_report;
void dc_render_frustum_cases(dc_frustum_report *);
#endif
