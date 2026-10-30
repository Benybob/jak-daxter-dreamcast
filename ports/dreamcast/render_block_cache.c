/* Validate once, retain exact whole blocks under an explicit budget (AI-assisted), ISC. */
#include "render_block_cache.h"
#include "render_indexed.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#ifdef DC_RENDER_BLOCK_CACHE_TEST
extern void *dc_rbc_test_malloc(size_t);
extern void dc_rbc_test_free(void *);
extern FILE *dc_rbc_test_fopen(const char *,const char *);
extern int dc_rbc_test_fclose(FILE *);
#define ALLOC dc_rbc_test_malloc
#define FREE dc_rbc_test_free
#define OPEN dc_rbc_test_fopen
#define CLOSE dc_rbc_test_fclose
#else
#define ALLOC malloc
#define FREE free
#define OPEN fopen
#define CLOSE fclose
#endif
typedef struct {
 dc_render_entry entry;dc_render_block block;
 unsigned char *blob; /* indexed owners: DRX1 in place of block */
 uint32_t rank,retained;int valid,global;
} slot;
typedef struct {
 dc_render_catalog catalog;char root[DC_RENDER_CACHE_ROOT_MAX+1];
 uint32_t budget;slot slots[DC_RENDER_BLOCK_CACHE_SLOTS];
 dc_render_block_cache_stats stats;
 dc_render_block borrowed;int pinned,cached,decoded,indexed;
} state;
static int length(const char *p,unsigned max){unsigned i;if(!p)return -1;for(i=0;i<=max;++i)if(!p[i])return (int)i;return -1;}
static int hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int catalog_equal(const dc_render_catalog *a,const dc_render_catalog *b) {
#define EQ(n) if(a->n!=b->n)return 0
 EQ(storage);EQ(bytes);EQ(source_count);EQ(sector_count);EQ(global_count);EQ(replica_count);
 EQ(material_count);EQ(texture_count);EQ(texture_bytes);EQ(package_bytes);EQ(crc);
 EQ(pixel_offset);EQ(material_offset);EQ(entry_offset);
#undef EQ
 return !memcmp(&a->cell_size,&b->cell_size,4);
}
static void touch(state *s,unsigned i) {
 unsigned j;uint32_t previous=s->slots[i].valid?s->slots[i].rank:UINT32_MAX;
 for(j=0;j<DC_RENDER_BLOCK_CACHE_SLOTS;++j)if(j!=i&&s->slots[j].valid&&s->slots[j].rank<previous)++s->slots[j].rank;
 s->slots[i].rank=0;
}
static unsigned victim(const state *s) {
 unsigned i,best=0;for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i) {
  if(!s->slots[i].valid)return i;
  if(s->slots[i].rank>s->slots[best].rank)best=i;
 }return best;
}
static void evict(state *s,unsigned i) {
 slot *v=&s->slots[i];if(!v->valid)return;
 s->stats.bytes-=v->retained;s->stats.represented_bytes-=v->entry.raw_bytes;--s->stats.blocks;++s->stats.evictions;
 dc_render_block_free(&v->block);FREE(v->blob);memset(v,0,sizeof(*v));
}
/* Used only when a byte budget needs space: victim() prefers an empty slot. */
static unsigned oldest(const state *s) {
 unsigned i,best=DC_RENDER_BLOCK_CACHE_SLOTS;for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)
  if(s->slots[i].valid&&(best==DC_RENDER_BLOCK_CACHE_SLOTS||s->slots[i].rank>s->slots[best].rank))best=i;
 return best;
}
/* A validated block as DRX1, or NULL to bypass: allocation failure, a refusal,
 * or a result over budget. The block itself is untouched either way. */
static unsigned char *encode(state *s,const dc_render_block *block,uint32_t *bytes) {
 uint32_t scratch_bytes=dc_render_indexed_scratch_bytes(block->count),size=0;void *scratch;unsigned char *blob=NULL;
 if(!scratch_bytes||!(scratch=ALLOC(scratch_bytes)))return NULL;
 if(!dc_render_indexed_encode(block->records,block->count,scratch,scratch_bytes,NULL,0,&size)&&
    size<=s->budget&&(blob=ALLOC(size))!=NULL&&
    dc_render_indexed_encode(block->records,block->count,scratch,scratch_bytes,blob,size,bytes)) {FREE(blob);blob=NULL;}
 FREE(scratch);return blob;
}
static const char *load(void *user,const dc_render_catalog *catalog,const dc_render_entry *entry,
 int global,const char *path,dc_render_block *out) {
 dc_render_block_cache *owner=user;state *s=owner?owner->state:NULL;
 dc_render_block block={0};char expected[DC_RENDER_CACHE_PATH_MAX];FILE *file;const char *error,*sep;int n,closed;unsigned i;size_t root_bytes;uint32_t n_bytes=0;
 if(!s||!catalog||!entry||!path||!out||out->records||out->count||s->pinned)return "block cache arguments";
 if((global!=0&&global!=1)||!catalog_equal(catalog,&s->catalog))return "block cache identity";
 root_bytes=strlen(s->root);sep=(s->root[root_bytes-1]=='/'||s->root[root_bytes-1]=='\\')?"":"/";
 n=global?snprintf(expected,sizeof expected,"%s%sglobal.drs",s->root,sep):
  snprintf(expected,sizeof expected,"%s%ssectors/%" PRId32 "_%" PRId32 ".drs",s->root,sep,entry->ix,entry->iz);
 if(n<0||(unsigned)n>=sizeof expected||strcmp(expected,path))return "block cache path";
 for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid&&s->slots[i].global==global&&
     !memcmp(&s->slots[i].entry,entry,sizeof(*entry))) {
  slot *v=&s->slots[i];
  if(v->blob) {
   uint32_t count=0;block.records=v->entry.count?ALLOC((size_t)v->entry.count*DC_RENDER_FACE_BYTES):NULL;
   if(v->entry.count&&!block.records){++s->stats.failures;return "block cache allocation";}
   error=dc_render_indexed_decode(v->blob,v->retained,block.records,v->entry.count,&count);
   if(!error&&count!=v->entry.count)error="block cache indexed count";
   if(error){FREE(block.records);++s->stats.failures;return error;}
   block.count=count;++s->stats.decodes;touch(s,i);++s->stats.hits;
   *out=block;s->borrowed=block;s->pinned=1;s->cached=0;s->decoded=1;return NULL;
  }
  touch(s,i);++s->stats.hits;*out=v->block;s->borrowed=*out;s->pinned=1;s->cached=1;return NULL;
 }
 ++s->stats.misses;
 if(!s->indexed&&entry->raw_bytes<=s->budget) {
  while(s->stats.bytes>s->budget-entry->raw_bytes) {
   i=oldest(s);if(i==DC_RENDER_BLOCK_CACHE_SLOTS)return "block cache budget invariant";evict(s,i);
  }
 }
 file=OPEN(path,"rb");if(!file){++s->stats.failures;return DC_RENDER_CACHE_ERR_OPEN;}
 error=dc_render_block_load(file,catalog,entry,global,&block);closed=CLOSE(file);
 if(!error&&closed)error=DC_RENDER_CACHE_ERR_CLOSE;
 if(error){dc_render_block_free(&block);++s->stats.failures;return error;}
 ++s->stats.loads;
 if(s->indexed) {
  unsigned char *blob=encode(s,&block,&n_bytes);
  if(blob) {
   while(s->stats.bytes>s->budget-n_bytes) {
    i=oldest(s);if(i==DC_RENDER_BLOCK_CACHE_SLOTS){FREE(blob);dc_render_block_free(&block);return "block cache budget invariant";}evict(s,i);
   }
   i=victim(s);evict(s,i);touch(s,i);
   s->slots[i].entry=*entry;s->slots[i].blob=blob;s->slots[i].retained=n_bytes;s->slots[i].global=global;s->slots[i].valid=1;
   s->stats.bytes+=n_bytes;s->stats.represented_bytes+=entry->raw_bytes;++s->stats.blocks;
   if(s->stats.bytes>s->stats.peak_bytes)s->stats.peak_bytes=s->stats.bytes;
  }else ++s->stats.bypasses;
  s->cached=0;
 }else if(entry->raw_bytes<=s->budget) {
  i=victim(s);evict(s,i);touch(s,i);
  s->slots[i].entry=*entry;s->slots[i].block=block;s->slots[i].retained=entry->raw_bytes;s->slots[i].global=global;s->slots[i].valid=1;
  s->stats.bytes+=entry->raw_bytes;s->stats.represented_bytes+=entry->raw_bytes;++s->stats.blocks;
  if(s->stats.bytes>s->stats.peak_bytes)s->stats.peak_bytes=s->stats.bytes;
  s->cached=1;
 }else {++s->stats.bypasses;s->cached=0;}
 *out=block;s->borrowed=block;s->pinned=1;return NULL;
}
static void release(void *user,dc_render_block *block) {
 dc_render_block_cache *owner=user;state *s=owner?owner->state:NULL;
 if(!s||!s->pinned||!block||block->records!=s->borrowed.records||block->count!=s->borrowed.count)return;
 if(s->decoded)FREE(s->borrowed.records);else if(!s->cached)dc_render_block_free(&s->borrowed);
 memset(&s->borrowed,0,sizeof s->borrowed);s->pinned=0;s->cached=0;s->decoded=0;memset(block,0,sizeof(*block));
}
static const char *make(const dc_render_catalog *catalog,const char *root,
 const char *sha,uint32_t budget,int indexed,dc_render_block_cache *out) {
 state *s;int n;unsigned i;
 if(!out||out->state)return "block cache owned output";
 if(!catalog||!catalog->storage||catalog->bytes<256||!budget||budget>DC_RENDER_BLOCK_CACHE_MAX||
    (n=length(root,DC_RENDER_CACHE_ROOT_MAX))<1||length(sha,64)!=64)return "block cache arguments";
 for(i=0;i<32;++i){int hi=hex(sha[i*2]),lo=hex(sha[i*2+1]);if(hi<0||lo<0||(unsigned)(hi*16+lo)!=catalog->storage[164+i])return "block cache SHA";}
 s=ALLOC(sizeof(*s));if(!s)return "block cache allocation";
 memset(s,0,sizeof(*s));s->catalog=*catalog;memcpy(s->root,root,(unsigned)n+1u);s->budget=budget;s->indexed=indexed;
 s->stats.metadata_bytes=(uint32_t)sizeof(*s);out->state=s;return NULL;
}
const char *dc_render_block_cache_make(const dc_render_catalog *catalog,const char *root,
 const char *sha,uint32_t budget,dc_render_block_cache *out) {return make(catalog,root,sha,budget,0,out);}
const char *dc_render_block_cache_make_indexed(const dc_render_catalog *catalog,const char *root,
 const char *sha,uint32_t budget,dc_render_block_cache *out) {return make(catalog,root,sha,budget,1,out);}
dc_render_block_source dc_render_block_cache_source(dc_render_block_cache *owner) {
 dc_render_block_source source={owner,load,release};return source;
}
void dc_render_block_cache_measure(const dc_render_block_cache *owner,dc_render_block_cache_stats *out) {
 const state *s=owner?owner->state:NULL;if(!out)return;if(s)*out=s->stats;else memset(out,0,sizeof(*out));
}
void dc_render_block_cache_free(dc_render_block_cache *owner) {
 state *s;unsigned i;if(!owner||!(s=owner->state))return;
 if(s->pinned&&s->decoded)FREE(s->borrowed.records);else if(s->pinned&&!s->cached)dc_render_block_free(&s->borrowed);
 for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid){dc_render_block_free(&s->slots[i].block);FREE(s->slots[i].blob);}
 FREE(s);owner->state=NULL;
}
