/* Geometry-window-independent VRAM ownership (AI-assisted), ISC. */
#include "render_gpu.h"
#include <stdlib.h>
#include <string.h>
#ifdef DC_RENDER_GPU_TEST_ALLOC
extern void *dc_gpu_test_calloc(size_t,size_t);
extern void dc_gpu_test_free(void *);
extern pvr_ptr_t dc_gpu_test_vmalloc(size_t);
extern void dc_gpu_test_vfree(pvr_ptr_t);
#define GPU_CALLOC dc_gpu_test_calloc
#define GPU_FREE dc_gpu_test_free
#define VRAM_MALLOC dc_gpu_test_vmalloc
#define VRAM_FREE dc_gpu_test_vfree
#else
#define GPU_CALLOC calloc
#define GPU_FREE free
#define VRAM_MALLOC pvr_mem_malloc
#define VRAM_FREE pvr_mem_free
#endif
void dc_render_gpu_free(dc_render_gpu *gpu) {
 uint32_t i;if(!gpu)return;
 if(gpu->slots)for(i=0;i<gpu->count;++i)if(gpu->slots[i])VRAM_FREE(gpu->slots[i]);
 GPU_FREE(gpu->slots);memset(gpu,0,sizeof(*gpu));
}
const char *dc_render_gpu_make(const dc_render_catalog *catalog,dc_render_gpu *out) {
 dc_render_gpu made={0};uint32_t i;
 if(!out||!catalog||!catalog->storage||!catalog->texture_count||catalog->texture_count>256u)return "GPU catalog arguments";
 if(out->slots||out->count||out->colors||out->bytes)return "GPU catalog owned";
 /* Validate every upload before allocating or touching VRAM. All dimensions
  * enforce exact 32-byte SQ transfers, with aligned CPU words and VRAM. */
 for(i=0;i<catalog->texture_count;++i) {
  dc_render_texture t;
  if(!dc_render_texture_get(catalog,i,&t)||!t.pixels||((uintptr_t)t.pixels&3u)
    ||t.width<8u||t.width>64u||t.height<8u||t.height>64u||(t.width&(t.width-1u))||(t.height&(t.height-1u))
    ||(t.format<1u||t.format>3u)||t.bytes!=t.width*t.height*(t.format==2u?1u:2u)||(t.bytes&31u))return "GPU catalog texture";
  if(t.format!=2u){made.bytes+=t.bytes;++made.colors;}
 }
 if(made.bytes>2097152u)return "GPU catalog budget";
 made.count=catalog->texture_count;made.catalog_crc=catalog->crc;
 memcpy(made.source_sha256,catalog->storage+164,32);
 made.slots=GPU_CALLOC(made.count,sizeof(*made.slots));if(!made.slots)return "GPU slot allocation";
 for(i=0;i<made.count;++i) {
  dc_render_texture t;dc_render_texture_get(catalog,i,&t);
  if(t.format==2u)continue;
  made.slots[i]=VRAM_MALLOC(t.bytes);if(!made.slots[i]){dc_render_gpu_free(&made);return "GPU VRAM allocation";}
  pvr_txr_load(t.pixels,made.slots[i],t.bytes);
 }
 *out=made;return NULL;
}
