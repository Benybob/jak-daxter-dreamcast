/* Host resident-owner lifetime cases, not GPU emulation (AI-assisted), ISC. */
#include "render_gpu_cases.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct {void *raw,*aligned;} block;
static block allocations[512];
pvr_ptr_t pvr_mem_malloc(size_t n) {
 uint32_t i;void *raw=malloc(n+32u);void *p;
 if(!raw)return NULL;
 p=(void *)(((uintptr_t)raw+31u)&~(uintptr_t)31u);
 for(i=0;i<512u;++i)if(!allocations[i].raw){allocations[i].raw=raw;allocations[i].aligned=p;return p;}
 free(raw);return NULL;
}
void pvr_mem_free(pvr_ptr_t p) {
 uint32_t i;for(i=0;i<512u;++i)if(allocations[i].aligned==p){free(allocations[i].raw);allocations[i].raw=NULL;allocations[i].aligned=NULL;return;}
}
void pvr_txr_load(const void *src,pvr_ptr_t dst,uint32_t n) {memcpy(dst,src,n);}
int main(int argc,char **argv) {
 dc_render_catalog c={0};dc_gpu_report r;FILE *f;const char *error;
 if(argc!=2)return 2;
 f=fopen(argv[1],"rb");if(!f)return 3;
 error=dc_render_catalog_load(f,&c);if(fclose(f)&&!error)error="close";
 if(error){fprintf(stderr,"%s\n",error);dc_render_catalog_free(&c);return 1;}
 dc_render_gpu_cases(&c,&r);dc_render_catalog_free(&c);
 printf("{\"total\":%u,\"passed\":%u,\"failed_line\":%u,\"colors\":%u,\"bytes\":%u,\"peak_vram\":%u,\"peak_cpu\":%u}\n",
  (unsigned)r.total,(unsigned)r.passed,(unsigned)r.failed_line,(unsigned)r.colors,(unsigned)r.bytes,(unsigned)r.peak_vram,(unsigned)r.peak_cpu);
 return r.failed_line?1:0;
}
