/* Native combined-owner failure/reuse oracle (AI-assisted), ISC. */
#include <kos.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "training_render.h"
#include "build/training_owner_fixture.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
typedef struct {void *p;size_t n;} allocation;
static allocation slots[16];static uint32_t calls,fail_at,live,peak,bad_free;
static dc_training_render owner,before,after;
static unsigned char saved_faces[2*116];static dc_render_vertex saved_views[6];
static void *track(void *p,size_t n) {
 unsigned i;if(!p)return NULL;
 for(i=0;i<16;++i)if(!slots[i].p){slots[i]=(allocation){p,n};live+=(uint32_t)n;if(live>peak)peak=live;return p;}
 free(p);return NULL;
}
void *dc_base_malloc(size_t n){if(++calls==fail_at)return NULL;return track(malloc(n),n);}
void *dc_base_calloc(size_t n,size_t s){if(++calls==fail_at)return NULL;return track(calloc(n,s),n*s);}
void *dc_base_memalign(size_t a,size_t n){if(++calls==fail_at)return NULL;return track(memalign(a,n),n);}
void dc_base_free(void *p){unsigned i;if(!p)return;
 for(i=0;i<16;++i)if(slots[i].p==p){live-=(uint32_t)slots[i].n;slots[i].p=NULL;free(p);return;}
 ++bad_free;
}
static int check(int ok,unsigned line) {++dc_backend.total;if(ok){++dc_backend.passed;return 1;}if(!dc_backend.failed_case)dc_backend.failed_case=line;return 0;}
#define CHECK(ok) check(!!(ok),__LINE__)
static int zero(const void *p,size_t n){const unsigned char *b=p;while(n--)if(*b++)return 0;return 1;}
static void snapshot(void) {
 memcpy(&before,&owner,sizeof before);memcpy(saved_faces,owner.window.faces,sizeof saved_faces);memcpy(saved_views,owner.frame.vertices,sizeof saved_views);
}
static int unchanged_except_refusals(void) {
 memcpy(&after,&owner,sizeof after);after.refusals=before.refusals;
 return !memcmp(&after,&before,sizeof before);
}
static void test_root(const char *root,uint32_t baseline) {
 dc_render_camera camera={{0,0,0},0,0,4};const char *error;uint32_t held;unsigned n;
 error=dc_training_render_make(root,owner_source_sha,&owner);if(!CHECK(error==NULL))return;
 CHECK(owner.initialized==1&&!owner.window.valid&&!owner.frame.valid);
 error=dc_training_render_update(&owner,&camera);if(!CHECK(error==NULL))goto done;
 CHECK(owner.commits==1&&owner.window.valid==1&&owner.window.ready==1&&owner.frame.ready==1);
 CHECK(owner.window.count==2&&crc32(0,owner.window.faces,232)==OWNER_INITIAL_CRC);
 CHECK(owner.gpu.slots&&owner.gpu.bytes==128);
 snapshot();error=dc_training_render_make(root,owner_source_sha,&owner);CHECK(error!=NULL);CHECK(!memcmp(&owner,&before,sizeof before));
 camera.target.x=.5f;error=dc_training_render_update(&owner,&camera);CHECK(error==NULL);
 CHECK(owner.commits==1&&owner.window.faces==before.window.faces&&!memcmp(&owner.window,&before.window,sizeof owner.window));
 CHECK(owner.frame.vertices[0].view.x==-.125f);
 camera.target.y=NAN;snapshot();error=dc_training_render_update(&owner,&camera);CHECK(error!=NULL&&owner.refusals==before.refusals+1);
 CHECK(owner.frame.ready==0&&!memcmp(owner.frame.vertices,saved_views,sizeof saved_views));
 {dc_render_base_stats stats={0};CHECK(dc_render_base_draw(&owner.bank,&owner.frame,PVR_LIST_OP_POLY,&stats)==0&&stats.faces==0);}
 camera.target.y=0;CHECK(dc_training_render_update(&owner,&camera)==NULL&&owner.frame.ready==1);
 camera.target.x=3;
 for(n=1;n<=2;++n) {
  snapshot();held=live;calls=0;fail_at=n;error=dc_training_render_update(&owner,&camera);fail_at=0;
  CHECK(error!=NULL&&owner.refusals==before.refusals+1);CHECK(unchanged_except_refusals());
  CHECK(!memcmp(owner.window.faces,saved_faces,sizeof saved_faces));CHECK(!memcmp(owner.frame.vertices,saved_views,sizeof saved_views));
  CHECK(live==held&&bad_free==0);CHECK(owner.gpu.slots==before.gpu.slots);
 }
 error=dc_training_render_update(&owner,&camera);CHECK(error==NULL);
 CHECK(owner.commits==2&&owner.window.coverage.x0==4.-18.4&&owner.window.count==2);
 CHECK(owner.gpu.slots==before.gpu.slots&&owner.gpu.bytes==128);
 snapshot();held=live;camera.target.x=20;error=dc_training_render_update(&owner,&camera);
 CHECK(error!=NULL&&owner.refusals==before.refusals+1);CHECK(unchanged_except_refusals());
 CHECK(!memcmp(owner.window.faces,saved_faces,sizeof saved_faces));CHECK(!memcmp(owner.frame.vertices,saved_views,sizeof saved_views));
 CHECK(live==held&&bad_free==0);CHECK(owner.gpu.slots==before.gpu.slots);
 camera.target.x=0;camera.target.z=100;error=dc_training_render_update(&owner,&camera);CHECK(error==NULL);
 CHECK(owner.window.count==0&&!owner.window.faces&&owner.frame.valid&&owner.frame.ready&&!owner.frame.vertices&&!owner.frame.order);
 CHECK(owner.commits==3&&owner.gpu.slots==before.gpu.slots);
 memcpy(&before,&owner,sizeof before);held=live;camera.target.x=40;camera.target.z=0;error=dc_training_render_update(&owner,&camera);
 CHECK(error!=NULL&&owner.refusals==before.refusals+1);CHECK(unchanged_except_refusals());CHECK(live==held);
done:
 pvr_wait_ready();dc_training_render_free(&owner);dc_training_render_free(&owner);
 CHECK(zero(&owner,sizeof owner));CHECK(live==0&&bad_free==0&&pvr_mem_available()==baseline);
}
int main(void) {
 pvr_init_params_t p={.opb_sizes={PVR_BINSIZE_32,0,0,0,PVR_BINSIZE_32},.vertex_buf_size=512*1024,.opb_overflow_count=3};
 uint32_t baseline=0;const char *error;char wrong[65];pvr_ptr_t prime;
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 if(!CHECK(pvr_init(&p)==0))goto terminal;
 prime=pvr_mem_malloc(32);if(!CHECK(prime!=NULL))goto terminal;pvr_mem_free(prime);baseline=pvr_mem_available();
 memset(wrong,'0',64);wrong[64]=0;error=dc_training_render_make("/rd/missing",wrong,&owner);
 CHECK(error!=NULL);CHECK(zero(&owner,sizeof owner));CHECK(live==0&&pvr_mem_available()==baseline);
 test_root("/rd/missing",baseline);test_root("/rd/trailing",baseline);
terminal:
 dc_backend.observed_lo=peak;dc_backend.observed_hi=live;
 snprintf((char *)dc_backend.description,80,"Combined render owner rollback/reuse line=%u",(unsigned)dc_backend.failed_case);
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);return 0;
}
