/* Resident catalog lifetime/failure oracle (AI-assisted), ISC. */
#ifndef DC_RENDER_GPU_CASES_H
#define DC_RENDER_GPU_CASES_H
#include "render_gpu.h"
typedef struct {uint32_t total,passed,failed_line,colors,bytes,peak_vram,peak_cpu;} dc_gpu_report;
void dc_render_gpu_cases(const dc_render_catalog *,dc_gpu_report *);
#endif
