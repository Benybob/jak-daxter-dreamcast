/* CPU and VRAM fault injection; exact VRAM readback (AI-assisted), ISC. */
#include "render_gpu_cases.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
static uint32_t attempt,fail_at,live_cpu,live_vram,peak_cpu,peak_vram;
typedef struct {void *p;uint32_t bytes;} tracked;
static tracked vram[512];
void *dc_gpu_test_calloc(size_t n,size_t size) {
 void *p;++attempt;if(attempt==fail_at)return NULL;p=calloc(n,size);if(p){live_cpu+=(uint32_t)(n*size);if(live_cpu>peak_cpu)peak_cpu=live_cpu;}return p;
}
/* There is exactly one slot-array allocation per owner. The caller's CPU
 * catalog uses normal allocation and is never routed through this test hook. */
static uint32_t slots_bytes;
void dc_gpu_test_free(void *p) {if(p){live_cpu-=slots_bytes;free(p);}}
pvr_ptr_t dc_gpu_test_vmalloc(size_t n) {
 pvr_ptr_t p;uint32_t i;++attempt;if(attempt==fail_at)return NULL;
 p=pvr_mem_malloc(n);if(!p)return NULL;
 for(i=0;i<512u;++i)if(!vram[i].p){vram[i].p=p;vram[i].bytes=(uint32_t)n;live_vram+=(uint32_t)n;
  if(live_vram>peak_vram)peak_vram=live_vram;
  return p;}
 pvr_mem_free(p);return NULL;
}
void dc_gpu_test_vfree(pvr_ptr_t p) {
 uint32_t i;for(i=0;i<512u;++i)if(vram[i].p==p){live_vram-=vram[i].bytes;vram[i].p=NULL;vram[i].bytes=0;pvr_mem_free(p);return;}
}
static int equal_pixels(const dc_render_catalog *c,const dc_render_gpu *gpu) {
 uint32_t i,j;
 if(gpu->count!=c->texture_count||gpu->catalog_crc!=c->crc||memcmp(gpu->source_sha256,c->storage+164,32))return 0;
 for(i=0;i<gpu->count;++i) {
  dc_render_texture t;dc_render_texture_get(c,i,&t);
  if(t.format==2u){if(gpu->slots[i])return 0;continue;}
  if(!gpu->slots[i])return 0;
  for(j=0;j<t.bytes;j+=4u){uint32_t word;memcpy(&word,t.pixels+j,4);
   if(((volatile const uint32_t *)gpu->slots[i])[j/4u]!=word)return 0;}
 }
 return 1;
}
static void check(dc_gpu_report *r,int ok,uint32_t line){++r->total;if(ok)++r->passed;else if(!r->failed_line)r->failed_line=line;}
#define CHECK(ok) check(r,!!(ok),__LINE__)
void dc_render_gpu_cases(const dc_render_catalog *c,dc_gpu_report *r) {
 dc_render_gpu old={0},fresh={0},snapshot,zero={0};uint32_t i,colors=0,bytes=0,source_crc;
 memset(r,0,sizeof(*r));attempt=fail_at=live_cpu=live_vram=peak_cpu=peak_vram=0;memset(vram,0,sizeof(vram));
 slots_bytes=c->texture_count*sizeof(pvr_ptr_t);source_crc=(uint32_t)crc32(0L,c->storage,c->bytes);
 CHECK(dc_render_gpu_make(NULL,&fresh)!=NULL&&dc_render_gpu_make(c,NULL)!=NULL);
 CHECK(!memcmp(&fresh,&zero,sizeof(fresh)));
 for(i=0;i<c->texture_count;++i){dc_render_texture t;dc_render_texture_get(c,i,&t);if(t.format==1u){++colors;bytes+=t.bytes;}}
 CHECK(dc_render_gpu_make(c,&old)==NULL);if(!old.slots)return;
 CHECK(old.colors==colors&&old.bytes==bytes);CHECK(equal_pixels(c,&old));
 snapshot=old;CHECK(dc_render_gpu_make(c,&old)!=NULL);CHECK(!memcmp(&snapshot,&old,sizeof(old)));
 CHECK((uint32_t)crc32(0L,c->storage,c->bytes)==source_crc);
 for(i=1;i<=colors+1u;++i) {
  attempt=0;fail_at=i;CHECK(dc_render_gpu_make(c,&fresh)!=NULL);
  CHECK(!memcmp(&fresh,&zero,sizeof(fresh)));CHECK(!memcmp(&old,&snapshot,sizeof(old))&&equal_pixels(c,&old));
  CHECK(live_cpu==slots_bytes&&live_vram==bytes);
  CHECK((uint32_t)crc32(0L,c->storage,c->bytes)==source_crc);
 }
 fail_at=0;CHECK(dc_render_gpu_make(c,&fresh)==NULL);CHECK(equal_pixels(c,&old)&&equal_pixels(c,&fresh));
 CHECK(live_vram==2u*bytes&&live_cpu==2u*slots_bytes);
 dc_render_gpu_free(&old);CHECK(equal_pixels(c,&fresh));CHECK(live_vram==bytes&&live_cpu==slots_bytes);
 dc_render_gpu_free(&fresh);dc_render_gpu_free(&fresh);dc_render_gpu_free(NULL);
 CHECK(!memcmp(&fresh,&zero,sizeof(fresh))&&live_vram==0u&&live_cpu==0u);
 CHECK((uint32_t)crc32(0L,c->storage,c->bytes)==source_crc);
 r->colors=colors;r->bytes=bytes;r->peak_vram=peak_vram;r->peak_cpu=peak_cpu;
}
