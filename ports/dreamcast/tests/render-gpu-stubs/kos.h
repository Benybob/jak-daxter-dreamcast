/* Tiny PVR allocation/upload stand-in, no raster emulation (AI-assisted), ISC. */
#ifndef DC_RENDER_GPU_TEST_KOS_H
#define DC_RENDER_GPU_TEST_KOS_H
#include <stdint.h>
#include <stddef.h>
typedef void *pvr_ptr_t;
pvr_ptr_t pvr_mem_malloc(size_t);
void pvr_mem_free(pvr_ptr_t);
void pvr_txr_load(const void *,pvr_ptr_t,uint32_t);
#endif
