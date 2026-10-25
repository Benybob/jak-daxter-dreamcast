/* Independent cache fault/rollback bridge, no game data (AI-assisted), ISC. */
#include "../render_cache.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {size_t bytes;uintptr_t marker;} mem;
typedef struct {dc_render_catalog catalog;dc_render_window window;} context;
static uint32_t live,peak,allocs,opens,closes,reads,files,alloc_fail,open_fail,close_fail,read_fail,io_error,bad_free;
static const char *last_error;
void *dc_render_cache_test_malloc(size_t n) {
 mem *p;++allocs;if(allocs==alloc_fail)return NULL;p=malloc(sizeof(*p)+n);if(!p)return NULL;
 p->bytes=n;p->marker=0x52434348u;live+=(uint32_t)n;if(live>peak)peak=live;return p+1;
}
void dc_render_cache_test_free(void *p) {
 mem *h;if(!p)return;h=(mem *)p-1;
 if(h->marker!=0x52434348u){++bad_free;return;}
 live-=(uint32_t)h->bytes;h->marker=0;free(h);
}
void *dc_render_test_malloc(size_t n){return dc_render_cache_test_malloc(n);}
void dc_render_test_free(void *p){dc_render_cache_test_free(p);}
FILE *dc_render_cache_test_fopen(const char *path,const char *mode) {
 FILE *f;++opens;if(opens==open_fail)return NULL;f=fopen(path,mode);if(f)++files;return f;
}
int dc_render_cache_test_fclose(FILE *f) {
 int actual;uint32_t n=++closes;actual=fclose(f);--files;return n==close_fail?EOF:actual;
}
size_t dc_render_test_fread(void *p,size_t size,size_t count,FILE *f) {
 ++reads;if(reads==read_fail){io_error=1;return 0;}return fread(p,size,count,f);
}
int dc_render_test_fgetc(FILE *f){++reads;if(reads==read_fail){io_error=1;return EOF;}return fgetc(f);}
int dc_render_test_ferror(FILE *f){return io_error||ferror(f);}
void dc_rc_arm(uint32_t af,uint32_t of,uint32_t cf,uint32_t rf) {
 allocs=opens=closes=reads=io_error=0;peak=live;alloc_fail=af;open_fail=of;close_fail=cf;read_fail=rf;
}
static const char *load_catalog(const char *path,dc_render_catalog *out) {
 FILE *f=fopen(path,"rb");const char *e;if(!f)return "catalog open";
 e=dc_render_catalog_load(f,out);if(fclose(f)&&!e)e="catalog close";return e;
}
const char *dc_rc_error(void){return last_error;}
void *dc_rc_new(const char *path) {
 context *c=calloc(1,sizeof(*c));if(!c){last_error="context allocation";return NULL;}
 dc_rc_arm(0,0,0,0);last_error=load_catalog(path,&c->catalog);
 if(last_error){dc_render_catalog_free(&c->catalog);free(c);return NULL;}
 dc_render_window_init(&c->window);return c;
}
static uint32_t fingerprint(const dc_render_window *w) {
 uLong hash=crc32(0L,NULL,0);
 if(w->faces)hash=crc32(hash,w->faces,w->count*DC_RENDER_FACE_BYTES);
 if(w->root)hash=crc32(hash,(const Bytef *)w->root,(uInt)strlen(w->root)+1u);
 if(w->source_sha)hash=crc32(hash,(const Bytef *)w->source_sha,(uInt)strlen(w->source_sha)+1u);
 return (uint32_t)hash;
}
const char *dc_rc_load(void *ptr,const char *root,const double *bounds,const char *new_catalog,const char *override_sha) {
 context *c=ptr;dc_render_catalog candidate={0};const dc_render_catalog *use;
 dc_render_window before;uint32_t before_crc;const char *error;char sha[65];dc_render_query query;unsigned i;
 if(!c)return "context null";
 memcpy(&before,&c->window,sizeof(before));before_crc=fingerprint(&before);use=&c->catalog;
 if(new_catalog){
  error=load_catalog(new_catalog,&candidate);if(error){dc_render_catalog_free(&candidate);return error;}
  use=&candidate;
 }
 for(i=0;i<32u;++i){unsigned v=use->storage[164u+i];sha[i*2u]="0123456789abcdef"[v>>4];sha[i*2u+1u]="0123456789abcdef"[v&15u];}sha[64]=0;
 if(bounds){query.x0=bounds[0];query.z0=bounds[1];query.x1=bounds[2];query.z1=bounds[3];}
 error=dc_render_window_retarget(use,root,override_sha?override_sha:sha,bounds?&query:NULL,&c->window);
 if(error&&(memcmp(&before,&c->window,sizeof(before))||fingerprint(&c->window)!=before_crc))error="CACHE ROLLBACK MUTATED";
 if(!error&&new_catalog){dc_render_catalog_free(&c->catalog);c->catalog=candidate;memset(&candidate,0,sizeof(candidate));}
 dc_render_catalog_free(&candidate);return error;
}
void dc_rc_stats(void *ptr,uint32_t *out) {
 context *c=ptr;
 out[0]=allocs;out[1]=opens;out[2]=closes;out[3]=reads;out[4]=files;out[5]=live;out[6]=peak;out[7]=bad_free;
 out[8]=c?c->window.count:0;out[9]=c?(uint32_t)c->window.valid:0;out[10]=c?(uint32_t)c->window.ready:0;
 out[11]=c?(uint32_t)crc32(0L,c->window.faces,c->window.count*DC_RENDER_FACE_BYTES):0;
}
uint32_t dc_rc_copy(void *ptr,void *out,uint32_t cap) {
 context *c=ptr;uint32_t bytes;if(!c)return 0;bytes=c->window.count*DC_RENDER_FACE_BYTES;
 if(cap<bytes||(!out&&bytes))return 0;
 if(bytes)memcpy(out,c->window.faces,bytes);
 return bytes;
}
void dc_rc_close(void *ptr) {
 context *c=ptr;if(!c)return;dc_render_window_free(&c->window);dc_render_window_free(&c->window);
 dc_render_catalog_free(&c->catalog);free(c);
}
