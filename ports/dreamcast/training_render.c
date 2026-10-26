/* Transactional geometry/frame pair, static GPU bank (AI-assisted), ISC. */
#include "training_render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int digit(char c) {
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static int text_length(const char *text,unsigned max) {
 unsigned n;if(!text)return -1;
 for(n=0;n<=max;++n)if(!text[n])return (int)n;
 return -1;
}
void dc_training_render_free(dc_training_render *r) {
 if(!r)return;
 dc_render_base_frame_free(&r->frame);dc_render_window_free(&r->window);
 dc_render_base_bank_free(&r->bank);dc_render_gpu_free(&r->gpu);dc_render_block_cache_free(&r->blocks);dc_render_catalog_free(&r->catalog);
 memset(r,0,sizeof(*r));
}
const char *dc_training_render_make(const char *root,const char *sha,dc_training_render *out) {
 dc_training_render made={0};char path[512];const char *error;FILE *file;int length,closed;unsigned i;
 if(!out||(length=text_length(root,DC_RENDER_CACHE_ROOT_MAX))<1||text_length(sha,64)!=64)return "training render arguments";
 if(out->initialized||out->catalog.storage||out->gpu.slots||out->blocks.state||out->bank.headers||out->bank.materials||out->frame.vertices||out->frame.order||
    out->frame.valid||out->window.faces||out->window.root||out->window.source_sha||out->window.valid)return "training render owned";
 memcpy(made.root,root,(unsigned)length+1u);memcpy(made.source_sha,sha,65);
 if(snprintf(path,sizeof path,"%s/manifest.drm",root)<0)return "training render path";
 file=fopen(path,"rb");if(!file)return "training render catalog open";
 error=dc_render_catalog_load(file,&made.catalog);closed=fclose(file);
 if(!error&&closed)error="training render catalog close";
 if(error)goto fail;
 for(i=0;i<32;++i) {
  int hi=digit(sha[i*2]),lo=digit(sha[i*2+1]);
  if(hi<0||lo<0||(unsigned)(hi*16+lo)!=made.catalog.storage[164+i]){error="training render source binding";goto fail;}
 }
 error=dc_render_gpu_make(&made.catalog,&made.gpu);if(error)goto fail;
 error=dc_render_base_bank_make(&made.catalog,&made.gpu,&made.bank);if(error)goto fail;
 error=dc_render_block_cache_make(&made.catalog,root,sha,DC_RENDER_BLOCK_CACHE_MAX,&made.blocks);if(error)goto fail;
 made.initialized=1;*out=made;return NULL;
fail:dc_training_render_free(&made);return error;
}
const char *dc_training_render_update(dc_training_render *r,const dc_render_camera *camera) {
 dc_render_query query,needed;dc_render_window candidate={0};dc_render_base_frame frame={0};const char *error;
 double x,z;
 dc_render_block_source source;
 if(!r||r->initialized!=1||!camera)return "training render update arguments";
 if(!isfinite(camera->target.x)||!isfinite(camera->target.z)){++r->refusals;return "training render camera target";}
 x=camera->target.x;z=camera->target.z;needed=(dc_render_query){x-16,z-16,x+16,z+16};
 if(r->window.valid&&needed.x0>=r->window.coverage.x0&&needed.x1<=r->window.coverage.x1&&
    needed.z0>=r->window.coverage.z0&&needed.z1<=r->window.coverage.z1) {
  if(dc_render_base_prepare(&r->frame,&r->window,camera))return NULL;
  ++r->refusals;return "training render camera refused";
 }
 x=floor(x/4.+.5)*4.;z=floor(z/4.+.5)*4.;query=(dc_render_query){x-18.4,z-18.4,x+18.4,z+18.4};
 source=dc_render_block_cache_source(&r->blocks);
 error=dc_render_window_retarget_source(&r->catalog,r->root,r->source_sha,&query,&source,&candidate);if(error)goto fail;
 error=dc_render_base_frame_make(&candidate,&r->bank,&frame);if(error)goto fail;
 if(!dc_render_base_prepare(&frame,&candidate,camera)){error="training render candidate camera";goto fail;}
 pvr_wait_ready();dc_render_base_frame_free(&r->frame);dc_render_window_free(&r->window);
 r->window=candidate;r->frame=frame;++r->commits;return NULL;
fail:
 dc_render_base_frame_free(&frame);dc_render_window_free(&candidate);++r->refusals;return error;
}
