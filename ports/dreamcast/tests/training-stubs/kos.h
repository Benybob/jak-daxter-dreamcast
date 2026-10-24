/* Minimal host instrumentation for our GPU layer, not KOS emulation (AI-assisted). */
#ifndef DC_TRAINING_TEST_KOS_H
#define DC_TRAINING_TEST_KOS_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef void *pvr_ptr_t;
typedef struct {uint32_t words[8];} pvr_poly_hdr_t;
typedef struct {uint32_t flags;float x,y,z,u,v;uint32_t argb,oargb;} pvr_vertex_t;
typedef struct {struct {int culling,shading;} gen;struct {int uv_clamp,uv_flip;} txr;} pvr_poly_cxt_t;
enum {PVR_LIST_OP_POLY=0,PVR_LIST_PT_POLY=4,PVR_TXRFMT_ARGB1555=1,PVR_TXRFMT_TWIDDLED=2,
 PVR_TXRFMT_VQ_ENABLE=4,PVR_FILTER_BILINEAR=1,PVR_CULLING_NONE=0,PVR_SHADE_GOURAUD=1,
 PVR_UVCLAMP_U=1,PVR_UVCLAMP_V=2,PVR_UVFLIP_U=1,PVR_UVFLIP_V=2};
#define PVR_CMD_VERTEX UINT32_C(0xe0000000)
#define PVR_CMD_VERTEX_EOL UINT32_C(0xf0000000)
void *dc_test_malloc(size_t);
void *dc_test_calloc(size_t,size_t);
void *dc_test_memalign(size_t,size_t);
void dc_test_free(void *);
pvr_ptr_t pvr_mem_malloc(size_t);
void pvr_mem_free(pvr_ptr_t);
void pvr_txr_load(const void *,pvr_ptr_t,size_t);
void pvr_poly_cxt_txr(pvr_poly_cxt_t *,int,int,unsigned,unsigned,pvr_ptr_t,int);
void pvr_poly_compile(pvr_poly_hdr_t *,const pvr_poly_cxt_t *);
int pvr_prim(const void *,size_t);
#define malloc dc_test_malloc
#define calloc dc_test_calloc
#define free dc_test_free
#define memalign dc_test_memalign
#endif
