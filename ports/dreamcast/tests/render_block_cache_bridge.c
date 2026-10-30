/* Procedural host source/cache bridge with strict failure checks (AI-assisted), ISC. */
#include "../render_block_cache.h"
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
typedef struct {dc_render_catalog catalog;dc_render_block_cache blocks;dc_render_window window;
 dc_render_draw_window drawn;char root[241],sha[65];} context;
static uint32_t attempts[3],faults[3],metadata_live,files_live;
static const char *last;
void dc_rbc_arm(uint32_t a,uint32_t o,uint32_t c){faults[0]=a;faults[1]=o;faults[2]=c;memset(attempts,0,sizeof attempts);}
void *dc_rbc_test_malloc(size_t n){void *p;if(++attempts[0]==faults[0])return NULL;p=malloc(n);if(p)++metadata_live;return p;}
void dc_rbc_test_free(void *p){if(p){--metadata_live;free(p);}}
FILE *dc_rbc_test_fopen(const char *p,const char *m){FILE *f;if(++attempts[1]==faults[1])return NULL;f=fopen(p,m);if(f)++files_live;return f;}
int dc_rbc_test_fclose(FILE *f){int r=fclose(f);--files_live;if(++attempts[2]==faults[2])return -1;return r;}
void dc_rbc_close(void *p){context *c=p;if(!c)return;dc_render_draw_window_free(&c->drawn);dc_render_window_free(&c->window);dc_render_block_cache_free(&c->blocks);dc_render_block_cache_free(&c->blocks);dc_render_catalog_free(&c->catalog);free(c);}
const char *dc_rbc_error(void){return last;}
const char *dc_rbc_dw_make(void *p,uint32_t f,uint32_t v,uint32_t r){context *c=p;return dc_render_draw_window_make(&c->drawn,f,v,r);}
const char *dc_rbc_dw_build(void *p,const double *query){context *c=p;return dc_render_block_cache_window(&c->blocks,&c->catalog,(const dc_render_query *)query,&c->drawn);}
/* 1 camera ok, 0 NaN yaw; returns prepare's result, then prepared in the top bit. */
uint32_t dc_rbc_dw_prepare(void *p,int good){context *c=p;dc_render_camera camera;float nan=0.f;nan=nan/nan;memset(&camera,0,sizeof camera);camera.distance=4.f;if(!good)camera.yaw=nan;
 {uint32_t r=(uint32_t)dc_render_draw_window_prepare(&c->drawn,&camera);return r|c->drawn.prepared<<31;}}
uint32_t dc_rbc_dw_prepared(void *p){context *c=p;return c->drawn.prepared;}
uint32_t dc_rbc_dw_count(void *p){context *c=p;return c->drawn.count;}
uint32_t dc_rbc_dw_vertices(void *p){context *c=p;return c->drawn.vertex_count;}
void dc_rbc_dw_copy(void *p,unsigned char *out){context *c=p;uint32_t i;for(i=0;i<c->drawn.count;++i)dc_render_draw_window_face(&c->drawn,i,out+(size_t)i*116u);}
static void *open_owner(const char *root,uint32_t budget,int draw){
 context *c=calloc(1,sizeof(*c));char path[512];FILE *f;unsigned i;int closed;
 if(!c)return NULL;
 if(!root||strlen(root)>240){last="bridge root";goto fail;}
 strcpy(c->root,root);snprintf(path,sizeof path,"%s/manifest.drm",root);f=fopen(path,"rb");
 if(!f){last="bridge catalog open";goto fail;}
 last=dc_render_catalog_load(f,&c->catalog);closed=fclose(f);if(!last&&closed)last="bridge catalog close";if(last)goto fail;
 for(i=0;i<32;++i)snprintf(c->sha+i*2,3,"%02x",c->catalog.storage[164+i]);
 last=(draw?dc_render_block_cache_make_draw:dc_render_block_cache_make)(&c->catalog,root,c->sha,budget,&c->blocks);if(last)goto fail;
 return c;
fail:dc_rbc_close(c);return NULL;
}
void *dc_rbc_new(const char *root,uint32_t budget){return open_owner(root,budget,0);}
void *dc_rbc_new_draw(const char *root,uint32_t budget){return open_owner(root,budget,1);}
const char *dc_rbc_window(void *p,const double *query){
 context *c=p;dc_render_window before;dc_render_block_source source;unsigned char *bytes=NULL;const char *error;
 if(!c||!query)return "bridge window arguments";
 before=c->window;if(before.count){bytes=malloc(before.count*116u);if(!bytes)return "bridge snapshot";memcpy(bytes,before.faces,before.count*116u);}
 source=dc_render_block_cache_source(&c->blocks);
 error=dc_render_window_retarget_source(&c->catalog,c->root,c->sha,(const dc_render_query *)query,&source,&c->window);
 if(error&&(memcmp(&before,&c->window,sizeof before)||(bytes&&memcmp(bytes,c->window.faces,before.count*116u))))error="BLOCK SOURCE ROLLBACK MUTATED";
 free(bytes);return error;
}
void dc_rbc_stats(void *p,uint32_t *out){
 context *c=p;dc_render_block_cache_stats stats;memset(out,0,16*sizeof(*out));
 dc_render_block_cache_measure(c?&c->blocks:NULL,&stats);memcpy(out,&stats,sizeof stats);
 memcpy(out+10,attempts,sizeof attempts);out[13]=metadata_live;out[14]=files_live;
 if(c)out[15]=c->window.count;
}
uint32_t dc_rbc_copy(void *p,void *out,uint32_t capacity){context *c=p;uint32_t n=c->window.count*116u;if(n>capacity)return 0;if(n)memcpy(out,c->window.faces,n);return n;}
/* Invalid paths/catalog and overlapping borrow are refused before disk I/O. */
int dc_rbc_guards(void *p){
 context *c=p;dc_render_entry e;dc_render_block a={0},b={0};dc_render_block_source source=dc_render_block_cache_source(&c->blocks);
 dc_render_catalog wrong=c->catalog;const char *error;char path[512];int ok=1;
 if(!dc_render_global_get(&c->catalog,&e))return 0;
 snprintf(path,sizeof path,"%s/global.drs",c->root);
 ++wrong.crc;error=source.load(source.user,&wrong,&e,1,path,&a);ok=ok&&error&&!a.records&&!a.count;
 error=source.load(source.user,&c->catalog,&e,1,"wrong-root/global.drs",&a);ok=ok&&error&&!a.records&&!a.count;
 error=source.load(source.user,&c->catalog,&e,1,path,&a);if(error)return 0;
 error=source.load(source.user,&c->catalog,&e,1,path,&b);ok=ok&&error&&!b.records&&!b.count;
 source.release(source.user,&a);ok=ok&&!a.records&&!a.count;
 return ok;
}
