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
 unsigned char *compact; /* DRX2 in place of block when the owner retains them */
 uint32_t rank,bytes;int valid,global,held;
} slot;
typedef struct {
 dc_render_catalog catalog;char root[DC_RENDER_CACHE_ROOT_MAX+1];
 uint32_t budget;slot slots[DC_RENDER_BLOCK_CACHE_SLOTS];
 dc_render_block_cache_stats stats;
 dc_render_block borrowed;int pinned,cached,draw,expanded;
 const unsigned char *peeked;
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
 }
 best=DC_RENDER_BLOCK_CACHE_SLOTS;
 for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)
  if(!s->slots[i].held&&(best==DC_RENDER_BLOCK_CACHE_SLOTS||s->slots[i].rank>s->slots[best].rank))best=i;
 return best;
}
static void evict(state *s,unsigned i) {
 slot *v=&s->slots[i];if(!v->valid)return;
 s->stats.bytes-=v->bytes;--s->stats.blocks;++s->stats.evictions;
 dc_render_block_free(&v->block);FREE(v->compact);memset(v,0,sizeof(*v));
}
/* Used only when a byte budget needs space: victim() prefers an empty slot. */
static unsigned oldest(const state *s) {
 unsigned i,best=DC_RENDER_BLOCK_CACHE_SLOTS;for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)
  if(s->slots[i].valid&&!s->slots[i].held&&(best==DC_RENDER_BLOCK_CACHE_SLOTS||s->slots[i].rank>s->slots[best].rank))best=i;
 return best;
}
/* Zero normals, then retain the block as DRX2 if it can be built and it fits. */
static void retain_draw(state *s,const dc_render_entry *entry,int global,dc_render_block *block) {
 uint32_t count=block->count,bytes=0,slots=8,i;unsigned char *built,*kept=NULL;uint16_t *table;
 for(i=0;i<count;++i){unsigned v;for(v=0;v<3;++v)memset(block->records+(size_t)i*116u+8u+v*36u+20u,0,12);}
 s->cached=0;
 if(count>DC_RENDER_INDEXED_DRAW_MAX_FACES){++s->stats.bypasses;return;}
 while(slots<6u*count)slots<<=1;
 built=ALLOC(dc_render_indexed_draw_bound(count));table=ALLOC((size_t)slots*sizeof(*table));
 if(built&&table&&!dc_render_indexed_encode_draw(block->records,count,built,dc_render_indexed_draw_bound(count),table,slots,&bytes)&&
    bytes<=s->budget&&(kept=ALLOC(bytes)))memcpy(kept,built,bytes);
 FREE(table);FREE(built);
 if(!kept){++s->stats.bypasses;return;}
 while(s->stats.bytes>s->budget-bytes)evict(s,oldest(s));
 i=victim(s);evict(s,i);touch(s,i);
 s->slots[i].entry=*entry;s->slots[i].compact=kept;s->slots[i].bytes=bytes;s->slots[i].global=global;s->slots[i].valid=1;
 s->stats.bytes+=bytes;++s->stats.blocks;
 if(s->stats.bytes>s->stats.peak_bytes)s->stats.peak_bytes=s->stats.bytes;
}
/* A DRX2 file is held to what dc_render_block_load holds a sector to, as far as
 * it can be without the sector: the entry's face count, IDs in order and known. */
static int within(const unsigned char *p,float limit) {
 float value;memcpy(&value,p,4);return value>=-limit&&value<=limit;
}
static int faces_fit(const state *s,const dc_render_entry *entry,const unsigned char *blob,uint32_t bytes) {
 uint32_t count=0,i,previous=0,vertices;const unsigned char *f;
 if(dc_render_indexed_check_draw(blob,bytes,&count)||count!=entry->count)return 0;
 vertices=(uint32_t)blob[4]|(uint32_t)blob[5]<<8|(uint32_t)blob[6]<<16|(uint32_t)blob[7]<<24;
 for(i=0;i<vertices;++i) {
  const unsigned char *v=blob+12u+i*24u;
  if(!within(v,1000000.f)||!within(v+4,1000000.f)||!within(v+8,1000000.f)||
     !within(v+12,65536.f)||!within(v+16,65536.f))return 0;
 }
 f=blob+12u+vertices*24u;
 for(i=0;i<count;++i,f+=16) {
  uint32_t id=(uint32_t)f[0]|(uint32_t)f[1]<<8|(uint32_t)f[2]<<16|(uint32_t)f[3]<<24;
  uint32_t material=(uint32_t)f[4]|(uint32_t)f[5]<<8|(uint32_t)f[6]<<16|(uint32_t)f[7]<<24;
  if(id>=s->catalog.source_count||material>=s->catalog.material_count||(i&&id<=previous))return 0;
  previous=id;
 }
 return 1;
}
void (*dc_render_block_cache_probe)(void);
/* Into a slot from the .drx. Space is made first, so an old block and the new
 * one never overlap above the budget; a bad file costs only those evictions. */
static int read_draw_file(state *s,const dc_render_entry *entry,int global) {
 char path[DC_RENDER_CACHE_PATH_MAX];FILE *file;long size;size_t root_bytes=strlen(s->root);
 const char *sep=(s->root[root_bytes-1]=='/'||s->root[root_bytes-1]=='\\')?"":"/";
 unsigned char *blob=NULL;uint32_t bytes=0;int n,ok=0,room;unsigned i;
 n=global?snprintf(path,sizeof path,"%s%sglobal.drx",s->root,sep):
  snprintf(path,sizeof path,"%s%ssectors/%" PRId32 "_%" PRId32 ".drx",s->root,sep,entry->ix,entry->iz);
 if(n<0||(unsigned)n>=sizeof path)return 0;
 file=OPEN(path,"rb");if(!file)return 0;
 if(!fseek(file,0,SEEK_END)&&(size=ftell(file))>0&&(unsigned long)size<=s->budget&&!fseek(file,0,SEEK_SET)) {
  bytes=(uint32_t)size;room=1;
  while(room&&s->stats.bytes>s->budget-bytes) {
   i=oldest(s);if(i==DC_RENDER_BLOCK_CACHE_SLOTS)room=0;else evict(s,i);
  }
  if(room)blob=ALLOC(bytes);
  ok=blob&&fread(blob,1,bytes,file)==bytes&&fgetc(file)==EOF;
  if(blob&&dc_render_block_cache_probe)dc_render_block_cache_probe();
 }
 if(CLOSE(file))ok=0;
 if(ok&&!faces_fit(s,entry,blob,bytes))ok=0;
 if(!ok){FREE(blob);return 0;}
 i=victim(s);if(i==DC_RENDER_BLOCK_CACHE_SLOTS){FREE(blob);return 0;}
 evict(s,i);touch(s,i);++s->stats.misses;++s->stats.loads;
 s->slots[i].entry=*entry;s->slots[i].compact=blob;s->slots[i].bytes=bytes;s->slots[i].global=global;s->slots[i].valid=1;
 s->stats.bytes+=bytes;++s->stats.blocks;
 if(s->stats.bytes>s->stats.peak_bytes)s->stats.peak_bytes=s->stats.bytes;
 return 1;
}
static const char *load(void *user,const dc_render_catalog *catalog,const dc_render_entry *entry,
 int global,const char *path,dc_render_block *out) {
 dc_render_block_cache *owner=user;state *s=owner?owner->state:NULL;
 dc_render_block block={0};char expected[DC_RENDER_CACHE_PATH_MAX];FILE *file;const char *error,*sep;int n,closed;unsigned i;size_t root_bytes;
 if(!s||!catalog||!entry||!path||!out||out->records||out->count||s->pinned)return "block cache arguments";
 if((global!=0&&global!=1)||!catalog_equal(catalog,&s->catalog))return "block cache identity";
 root_bytes=strlen(s->root);sep=(s->root[root_bytes-1]=='/'||s->root[root_bytes-1]=='\\')?"":"/";
 n=global?snprintf(expected,sizeof expected,"%s%sglobal.drs",s->root,sep):
  snprintf(expected,sizeof expected,"%s%ssectors/%" PRId32 "_%" PRId32 ".drs",s->root,sep,entry->ix,entry->iz);
 if(n<0||(unsigned)n>=sizeof expected||strcmp(expected,path))return "block cache path";
 for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid&&s->slots[i].global==global&&
     !memcmp(&s->slots[i].entry,entry,sizeof(*entry))) {
  if(s->draw) {
   block.count=entry->count;
   if(block.count) {
    block.records=ALLOC((size_t)block.count*116u);
    if(!block.records){++s->stats.failures;return "block cache allocation";}
    dc_render_indexed_expand_draw(s->slots[i].compact,block.records);
   }
   touch(s,i);++s->stats.hits;*out=block;s->borrowed=block;s->pinned=1;s->expanded=1;return NULL;
  }
  touch(s,i);++s->stats.hits;*out=s->slots[i].block;s->borrowed=*out;s->pinned=1;s->cached=1;return NULL;
 }
 ++s->stats.misses;
 if(!s->draw&&entry->raw_bytes<=s->budget) {
  while(s->stats.bytes>s->budget-entry->raw_bytes) {
   i=oldest(s);if(i==DC_RENDER_BLOCK_CACHE_SLOTS)return "block cache budget invariant";evict(s,i);
  }
 }
 file=OPEN(path,"rb");if(!file){++s->stats.failures;return DC_RENDER_CACHE_ERR_OPEN;}
 error=dc_render_block_load(file,catalog,entry,global,&block);closed=CLOSE(file);
 if(!error&&closed)error=DC_RENDER_CACHE_ERR_CLOSE;
 if(error){dc_render_block_free(&block);++s->stats.failures;return error;}
 ++s->stats.loads;
 if(s->draw)retain_draw(s,entry,global,&block);
 else if(entry->raw_bytes<=s->budget) {
  i=victim(s);evict(s,i);touch(s,i);
  s->slots[i].entry=*entry;s->slots[i].block=block;s->slots[i].bytes=entry->raw_bytes;s->slots[i].global=global;s->slots[i].valid=1;
  s->stats.bytes+=entry->raw_bytes;++s->stats.blocks;
  if(s->stats.bytes>s->stats.peak_bytes)s->stats.peak_bytes=s->stats.bytes;
  s->cached=1;
 }else {++s->stats.bypasses;s->cached=0;}
 *out=block;s->borrowed=block;s->pinned=1;return NULL;
}
/* A DRX2 face begins with its source ID, so the select pass needs no expansion. */
static int peek(void *user,const dc_render_catalog *catalog,const dc_render_entry *entry,int global,
 const unsigned char **ids,uint32_t *stride) {
 dc_render_block_cache *owner=user;state *s=owner?owner->state:NULL;unsigned i;int fresh=0;
 if(!s||!s->draw||s->pinned||!catalog||!entry||!ids||!stride||!catalog_equal(catalog,&s->catalog))return 0;
 for(;;) {
  for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid&&s->slots[i].global==global&&
      !memcmp(&s->slots[i].entry,entry,sizeof(*entry))) {
   const unsigned char *c=s->slots[i].compact;
   uint32_t vertices=(uint32_t)c[4]|(uint32_t)c[5]<<8|(uint32_t)c[6]<<16|(uint32_t)c[7]<<24;
   touch(s,i);if(!fresh)++s->stats.hits;s->peeked=c;*ids=c+12u+vertices*24u;*stride=16u;return 1;
  }
  if(fresh||!read_draw_file(s,entry,global))return 0;
  fresh=1;
 }
}
static void face(void *user,uint32_t index,unsigned char *out) {
 dc_render_block_cache *owner=user;state *s=owner->state;
 dc_render_indexed_expand_draw_face(s->peeked,index,out);
}
static void release(void *user,dc_render_block *block) {
 dc_render_block_cache *owner=user;state *s=owner?owner->state:NULL;
 if(!s||!s->pinned||!block||block->records!=s->borrowed.records||block->count!=s->borrowed.count)return;
 if(s->expanded)FREE(s->borrowed.records);
 else if(!s->cached)dc_render_block_free(&s->borrowed);
 memset(&s->borrowed,0,sizeof s->borrowed);s->pinned=0;s->cached=0;s->expanded=0;memset(block,0,sizeof(*block));
}
static const char *make(const dc_render_catalog *catalog,const char *root,
 const char *sha,uint32_t budget,int draw,dc_render_block_cache *out) {
 state *s;int n;unsigned i;
 if(!out||out->state)return "block cache owned output";
 if(!catalog||!catalog->storage||catalog->bytes<256||!budget||budget>DC_RENDER_BLOCK_CACHE_MAX||
    (n=length(root,DC_RENDER_CACHE_ROOT_MAX))<1||length(sha,64)!=64)return "block cache arguments";
 for(i=0;i<32;++i){int hi=hex(sha[i*2]),lo=hex(sha[i*2+1]);if(hi<0||lo<0||(unsigned)(hi*16+lo)!=catalog->storage[164+i])return "block cache SHA";}
 s=ALLOC(sizeof(*s));if(!s)return "block cache allocation";
 memset(s,0,sizeof(*s));s->catalog=*catalog;memcpy(s->root,root,(unsigned)n+1u);s->budget=budget;s->draw=draw;
 s->stats.metadata_bytes=(uint32_t)sizeof(*s);out->state=s;return NULL;
}
const char *dc_render_block_cache_make(const dc_render_catalog *catalog,const char *root,
 const char *sha,uint32_t budget,dc_render_block_cache *out) {
 return make(catalog,root,sha,budget,0,out);
}
const char *dc_render_block_cache_make_draw(const dc_render_catalog *catalog,const char *root,
 const char *sha,uint32_t budget,dc_render_block_cache *out) {
 return make(catalog,root,sha,budget,1,out);
}
typedef struct {const unsigned char *vertices,*faces;uint32_t count,at,remap;int global;} cursor;
static uint32_t word(const unsigned char *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static const unsigned char *corner(const cursor *c,const unsigned char *f,unsigned j) {
 return c->vertices+(size_t)((uint32_t)f[8u+j*2u]|(uint32_t)f[9u+j*2u]<<8)*24u;
}
/* Closed bounds of the three corners against the query, as render_cache.c tests
 * a face: float coordinates compared as doubles. */
static int touches(const cursor *c,const unsigned char *f,const dc_render_query *q) {
 double x0=0,x1=0,z0=0,z1=0;unsigned j;
 for(j=0;j<3;++j) {
  const unsigned char *p=corner(c,f,j);float x,z;double a,b;
  memcpy(&x,p,4);memcpy(&z,p+8,4);a=x;b=z;
  if(!j){x0=x1=a;z0=z1=b;}
  else {if(a<x0)x0=a;if(a>x1)x1=a;if(b<z0)z0=b;if(b>z1)z1=b;}
 }
 return x0<=q->x1&&x1>=q->x0&&z0<=q->z1&&z1>=q->z0;
}
/* One merge in source-ID order. write 0 checks and counts; write 1 fills. */
static const char *merge(dc_render_draw_window *w,cursor *blocks,unsigned count,const dc_render_query *q,
 int write,uint32_t *out_faces,uint32_t *out_vertices) {
 uint32_t faces=0,vertices=0;unsigned i;
 for(i=0;i<count;++i)blocks[i].at=0;
 for(;;) {
  unsigned first=count,j;uint32_t id=0;const unsigned char *f;cursor *c;
  for(i=0;i<count;++i)if(blocks[i].at<blocks[i].count) {
   uint32_t here=word(blocks[i].faces+(size_t)blocks[i].at*16u);
   if(first==count||here<id){first=i;id=here;}
  }
  if(first==count)break;
  c=&blocks[first];f=c->faces+(size_t)c->at*16u;
  for(i=first+1;i<count;++i)if(blocks[i].at<blocks[i].count) {
   const unsigned char *other=blocks[i].faces+(size_t)blocks[i].at*16u;
   if(word(other)!=id)continue;
   if(!write) {
    if(c->global||blocks[i].global)return DC_RENDER_CACHE_ERR_ROLE;
    if(word(other+4)!=word(f+4))return DC_RENDER_CACHE_ERR_REPLICA;
    for(j=0;j<3;++j)if(memcmp(corner(&blocks[i],other,j),corner(c,f,j),24))return DC_RENDER_CACHE_ERR_REPLICA;
   }
   ++blocks[i].at;
  }
  if(touches(c,f,q)) {
   if(faces>=w->face_cap)return DC_RENDER_CACHE_ERR_FACE_CAP;
   if(write) {
    dc_render_draw_face *o=&w->faces[faces];o->id=id;o->material=(uint16_t)word(f+4);
    for(j=0;j<3;++j) {
     uint32_t index=(uint32_t)f[8u+j*2u]|(uint32_t)f[9u+j*2u]<<8;uint16_t *mapped=&w->remap[c->remap+index];
     if(!*mapped) {
      memcpy(w->vertices+(size_t)vertices*24u,c->vertices+(size_t)index*24u,24);*mapped=(uint16_t)++vertices;
     }
     o->v[j]=(uint16_t)(*mapped-1u);
    }
   }else for(j=0;j<3;++j) {
    uint32_t index=(uint32_t)f[8u+j*2u]|(uint32_t)f[9u+j*2u]<<8;uint16_t *mapped=&w->remap[c->remap+index];
    if(!*mapped) {
     if(vertices>=w->vertex_cap||vertices>=65535u)return "draw window vertex capacity";
     *mapped=(uint16_t)++vertices;
    }
   }
   ++faces;
  }
  ++c->at;
 }
 *out_faces=faces;*out_vertices=vertices;return NULL;
}
const char *dc_render_block_cache_window(dc_render_block_cache *owner,const dc_render_catalog *catalog,
 const dc_render_query *query,dc_render_draw_window *window) {
 state *s=owner?owner->state:NULL;
 static dc_render_entry entries[DC_RENDER_CACHE_LOCAL_CAP+1u];
 static cursor blocks[DC_RENDER_CACHE_LOCAL_CAP+1u];
 uint32_t count=0,faces=0,vertices=0,again=0,remap=0;unsigned n,i;const char *error;
 if(!s||!s->draw||s->pinned||!catalog||!query||!window||!window->faces||!catalog_equal(catalog,&s->catalog))return "block cache window arguments";
 error=dc_render_window_entries(catalog,query,&entries[0],&entries[1],&count);
 if(error)return error;
 if(window->valid&&window->catalog_crc==catalog->crc&&window->coverage.x0<=query->x0&&window->coverage.x1>=query->x1&&
    window->coverage.z0<=query->z0&&window->coverage.z1>=query->z1)return NULL;
 for(n=0;n<=count&&!error;++n) {
  int global=n==0,found=0;
  for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid&&s->slots[i].global==global&&
      !memcmp(&s->slots[i].entry,&entries[n],sizeof(entries[n]))) {
   touch(s,i);s->slots[i].held=1;++s->stats.hits;found=1;break;
  }
  if(!found) {
   if(!read_draw_file(s,&entries[n],global))error="block cache window residency";
   else for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid&&s->slots[i].global==global&&
      !memcmp(&s->slots[i].entry,&entries[n],sizeof(entries[n]))){s->slots[i].held=1;break;}
  }
  if(!error) {
   const unsigned char *c=s->slots[i].compact;uint32_t nv=word(c+4);
   blocks[n]=(cursor){c+12u,c+12u+nv*24u,word(c+8),0,remap,global};
   if(nv>window->remap_cap-remap)error="draw window remap capacity";
   remap+=nv;
  }
 }
 if(!error) {
  memset(window->remap,0,(size_t)remap*sizeof(*window->remap));
  error=merge(window,blocks,count+1u,query,0,&faces,&vertices);
 }
 if(!error) {
  memset(window->remap,0,(size_t)remap*sizeof(*window->remap));
  window->prepared=0;
  error=merge(window,blocks,count+1u,query,1,&faces,&again);
 }
 for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)s->slots[i].held=0;
 if(error)return error;
 window->count=faces;window->vertex_count=again;window->coverage=*query;window->catalog_crc=catalog->crc;window->valid=1;window->prepared=0;
 return NULL;
}
dc_render_block_source dc_render_block_cache_source(dc_render_block_cache *owner) {
 dc_render_block_source source={owner,load,release,peek,face};return source;
}
void dc_render_block_cache_measure(const dc_render_block_cache *owner,dc_render_block_cache_stats *out) {
 const state *s=owner?owner->state:NULL;if(!out)return;if(s)*out=s->stats;else memset(out,0,sizeof(*out));
}
void dc_render_block_cache_free(dc_render_block_cache *owner) {
 state *s;unsigned i;if(!owner||!(s=owner->state))return;
 if(s->pinned&&s->expanded)FREE(s->borrowed.records);
 else if(s->pinned&&!s->cached)dc_render_block_free(&s->borrowed);
 for(i=0;i<DC_RENDER_BLOCK_CACHE_SLOTS;++i)if(s->slots[i].valid){dc_render_block_free(&s->slots[i].block);FREE(s->slots[i].compact);}
 FREE(s);owner->state=NULL;
}
