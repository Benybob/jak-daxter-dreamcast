/* Host/native allocator + IO failure oracle for the exact reader (AI-assisted), ISC. */
#include "../render_package.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {size_t bytes;double alignment;} allocation;
static uint32_t live,peak,alloc_calls,read_calls,alloc_fail,read_fail,io_error;
int dc_render_test_begin(uint32_t af,uint32_t rf) {
 if(live)return 0;
 peak=alloc_calls=read_calls=io_error=0;alloc_fail=af;read_fail=rf;return 1;
}
void dc_render_test_stats(uint32_t *out) {
 out[0]=alloc_calls;out[1]=read_calls;out[2]=live;out[3]=peak;
}
void *dc_render_test_malloc(size_t n) {
 allocation *p;++alloc_calls;if(alloc_calls==alloc_fail)return NULL;
 p=(allocation *)malloc(sizeof(*p)+n);if(!p)return NULL;p->bytes=n;
 live+=(uint32_t)n;if(live>peak)peak=live;return p+1;
}
void dc_render_test_free(void *p) {if(p){allocation *h=(allocation *)p-1;live-=(uint32_t)h->bytes;free(h);}}
size_t dc_render_test_fread(void *p,size_t size,size_t count,FILE *f) {
 ++read_calls;if(read_calls==read_fail){io_error=1;return 0;}return fread(p,size,count,f);
}
int dc_render_test_fgetc(FILE *f) {++read_calls;if(read_calls==read_fail){io_error=1;return EOF;}return fgetc(f);}
int dc_render_test_ferror(FILE *f) {return io_error||ferror(f);}

/* Returns NULL success or static error; words may be NULL. Every call must end
 * at zero tracked bytes. Decoder's own pool is measured by its separate suite.
 * owned-refusal snapshots exercise the public immutable-output contract. */
const char *dc_render_test_check(const char *manifest,const char *sector,uint32_t index,int global,
                                uint32_t fail_alloc,uint32_t fail_io,uint32_t *words) {
 dc_render_catalog c={0},snapshot;dc_render_entry e;dc_render_block b={0},saved;
 FILE *f;const char *error=NULL;uint32_t i;
 if(live)return "reader leak before call";
 live=peak=alloc_calls=read_calls=io_error=0;alloc_fail=fail_alloc;read_fail=fail_io;
 if(words)memset(words,0,16u*sizeof(*words));
 f=fopen(manifest,"rb");if(!f)return "manifest open";
 error=dc_render_catalog_load(f,&c);if(fclose(f)&&!error)error="manifest close";
 if(error)goto done;
 snapshot=c;
 if(!dc_render_catalog_load(NULL,&c)||memcmp(&c,&snapshot,sizeof(c))){error="owned catalog mutated";goto done;}
 if(words){words[0]=c.source_count;words[1]=c.sector_count;words[2]=c.material_count;words[3]=c.texture_count;
   words[4]=c.texture_bytes;words[5]=c.bytes;words[6]=c.global_count;}
 for(i=0;i<c.texture_count;++i){dc_render_texture t;if(!dc_render_texture_get(&c,i,&t)){error="texture get";goto done;}}
 for(i=0;i<c.material_count;++i){dc_render_material m;if(!dc_render_material_get(&c,i,&m)){error="material get";goto done;}}
 if(sector) {
  if(!(global?dc_render_global_get(&c,&e):dc_render_entry_get(&c,index,&e))){error="entry get";goto done;}
  f=fopen(sector,"rb");if(!f){error="sector open";goto done;}
  error=dc_render_block_load(f,&c,&e,global,&b);if(fclose(f)&&!error)error="sector close";
  if(error)goto done;
  if(words){words[7]=b.count;words[8]=e.raw_bytes;words[9]=(uint32_t)crc32(0L,b.records,e.raw_bytes);}
  if(b.records){saved=b;if(!dc_render_block_load(NULL,&c,&e,global,&b)||memcmp(&saved,&b,sizeof(b))){error="owned block mutated";goto done;}}
 }
done:
 dc_render_block_free(&b);dc_render_block_free(&b);dc_render_catalog_free(&c);dc_render_catalog_free(&c);
 if(words){words[10]=alloc_calls;words[11]=read_calls;words[12]=live;words[13]=peak;words[14]=io_error;}
 if(live)return "reader leak after call";
 return error;
}
