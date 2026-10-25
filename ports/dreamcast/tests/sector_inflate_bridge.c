/* Exact allocator failures and cap oracle, test only (AI-assisted), ISC. */
#include <stdlib.h>
#include <stdint.h>
typedef union {long double alignment;size_t bytes;} dc_test_allocation;
static unsigned calls,fail_at,live;
static size_t bytes_live,peak;
static void *dc_inflate_test_malloc(size_t bytes){
 dc_test_allocation *a;++calls;if(fail_at && calls==fail_at)return NULL;
 a=malloc(sizeof(*a)+bytes);if(!a)return NULL;a->bytes=bytes;
 ++live;bytes_live+=bytes;if(bytes_live>peak)peak=bytes_live;return a+1;
}
static void dc_inflate_test_free(void *ptr){
 dc_test_allocation *a;if(!ptr)return;a=(dc_test_allocation *)ptr-1;
 bytes_live-=a->bytes;--live;free(a);
}
#define malloc dc_inflate_test_malloc
#define free dc_inflate_test_free
#include "../sector_inflate.c"
#undef malloc
#undef free
void dc_inflate_test_reset(unsigned n){calls=0;fail_at=n;peak=bytes_live;}
unsigned dc_inflate_test_calls(void){return calls;}
unsigned dc_inflate_test_live(void){return live;}
size_t dc_inflate_test_peak(void){return peak;}
int dc_inflate_test_pool_limits(void){
 dc_inflate_pool pool={0};void *a;int okay=1;
 okay &= pool_alloc(&pool,0xffffffffu,0xffffffffu)==NULL;
 okay &= pool_alloc(&pool,0,1)==NULL;
 okay &= pool_alloc(&pool,1,0)==NULL;
 a=pool_alloc(&pool,1,DC_INFLATE_WORKSPACE_MAX-sizeof(dc_inflate_block));
 okay &= a!=NULL && pool.live==DC_INFLATE_WORKSPACE_MAX;
 okay &= !a || (uintptr_t)a%sizeof(void *)==0;
 okay &= pool_alloc(&pool,1,1)==NULL;
 pool_release(&pool);return okay && !pool.head && !pool.live && !live;
}
