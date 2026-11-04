/* Real CPU readers/cache and slab over private fixture data (AI-assisted), ISC.
 * No GPU or original instruction execution. SH-4 GPU/header allocation sizes
 * are explicit; the host cache's larger pointers make its metadata conservative. */
#include "arena_slab.h"
#include "render_block_cache.h"
#include "instance_cells.h"
#include <stdint.h>
#include <string.h>
#include <assert.h>
void dc_arena_slab_init(void *,uint32_t);
extern volatile uint32_t dc_arena_slab_high_water,dc_arena_slab_failures;
static unsigned char memory[2621440+32];
static struct {FILE *file;void *token;} files[16];
FILE *dc_arena_fopen(const char *path,const char *mode){
 unsigned i;FILE *f=fopen(path,mode);if(!f)return NULL;
 for(i=0;i<16;++i)if(!files[i].file){
  void *p=dc_arena_malloc(8);if(!p){fclose(f);return NULL;}
  files[i].file=f;files[i].token=p;return f;
 }
 fclose(f);return NULL;
}
int dc_arena_fclose(FILE *f){unsigned i;for(i=0;i<16;++i)if(files[i].file==f){dc_arena_free(files[i].token);files[i].file=NULL;break;}return fclose(f);}
size_t dc_arena_fread(void *p,size_t b,size_t n,FILE *f){return fread(p,b,n,f);}
int dc_arena_fgetc(FILE *f){return fgetc(f);}
int dc_arena_fseek(FILE *f,long n,int w){return fseek(f,n,w);}
long dc_arena_ftell(FILE *f){return ftell(f);}
int dc_arena_ferror(FILE *f){return ferror(f);}
int main(int argc,char **argv){
 dc_render_catalog c={0};dc_render_block_cache cache={0};dc_render_draw_window w={0};
 dc_instance_package pkg={0};dc_instance_cells cells={0};
 FILE *f;char path[1024],sha[65];unsigned i;uint32_t cap,faces,vertices,remap,budget,records,bytes;
 unsigned char *data;double x,z,half;const char *error;
 if(argc!=10)return 2;
 cap=(uint32_t)strtoul(argv[2],NULL,10);faces=(uint32_t)strtoul(argv[3],NULL,10);
 vertices=(uint32_t)strtoul(argv[4],NULL,10);remap=(uint32_t)strtoul(argv[5],NULL,10);
 budget=(uint32_t)strtoul(argv[6],NULL,10);records=(uint32_t)strtoul(argv[7],NULL,10);
 bytes=(uint32_t)strtoul(argv[8],NULL,10);if(cap>2621440)return 2;
 dc_arena_slab_init((void *)(((uintptr_t)memory+31u)&~(uintptr_t)31u),cap);
 snprintf(path,sizeof path,"%s/manifest.drm",argv[1]);f=dc_arena_fopen(path,"rb");assert(f);
 error=dc_render_catalog_load(f,&c);assert(!error);assert(!dc_arena_fclose(f));
 /* SH-4 pointers=4, pvr_poly_hdr_t=32, base material=8. */
 assert(dc_arena_calloc(c.texture_count,4));assert(dc_arena_malloc(c.material_count*32));
 assert(dc_arena_calloc(c.material_count,8));
 for(i=0;i<32;++i)sprintf(sha+i*2,"%02x",c.storage[164+i]);
 error=dc_render_block_cache_make_draw(&c,argv[1],sha,budget,&cache);assert(!error);
 error=dc_render_draw_window_make(&w,faces,vertices,remap);assert(!error);
 data=dc_arena_malloc(bytes);assert(data);snprintf(path,sizeof path,"%s/package.dci",argv[1]);
 f=dc_arena_fopen(path,"rb");assert(f);assert(dc_arena_fread(data,1,bytes,f)==bytes);assert(!dc_arena_fclose(f));
 assert(!dc_instance_package_open(data,bytes,&pkg));
 assert(!dc_instance_cells_make(&cells,argv[1],16,records));
 assert(dc_arena_malloc(pkg.max_proto_verts*12));assert(dc_arena_malloc(pkg.proto_count*16));
 f=fopen(argv[9],"r");assert(f);
 while(fscanf(f,"%lf %lf %lf",&x,&z,&half)==3){
  dc_render_query q={x-half,z-half,x+half,z+half};
  error=dc_instance_cells_update(&cells,&pkg,x,z,80);if(error){fprintf(stderr,"%s\n",error);return 3;}
  error=dc_render_block_cache_window(&cache,&c,&q,&w);if(error){fprintf(stderr,"%s\n",error);return 4;}
 }
 fclose(f);assert(!dc_arena_slab_failures);
 printf("%lu\n",(unsigned long)dc_arena_slab_high_water);return 0;
}
