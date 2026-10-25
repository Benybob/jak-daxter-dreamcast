/* Independent cache/original payload oracle (AI-assisted), ISC. */
#ifndef DC_RENDER_CACHE_CASES_H
#define DC_RENDER_CACHE_CASES_H
#include <stdint.h>
typedef struct {uint32_t total,passed,failed_line,windows,max_faces,peak_bytes;} dc_render_cache_report;
void dc_render_cache_cases(const char *,dc_render_cache_report *);
#endif
