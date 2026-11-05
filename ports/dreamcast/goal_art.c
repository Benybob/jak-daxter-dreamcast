/* Native counterpart of Jak 1 work_v2 + c_symlink2, data only (AI-assisted), ISC. */
#include "goal_art.h"
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t data,size,link,length;} art_row;
#ifdef DC_ARENA_JAK_PLAYER
typedef struct {
  uint32_t step,used,symbol,type_word,prev_value,next_type;
  uint32_t symbol_type,symbol_type_tag,symbol_type_methods;
  /* Little-endian words at symbol-type object offsets 0, 4, 8, 12, 16, 20. */
  uint32_t symbol_type_header[6];
} dc_goal_art_probe_row;
volatile struct {uint32_t version,count;dc_goal_art_probe_row row[8];} dc_goal_art_symbol_probe={.version=1};
typedef char dc_goal_art_probe_bytes[sizeof dc_goal_art_symbol_probe==488?1:-1];
static uint32_t probe_mem(dc_goal_runtime *rt,uint32_t address,void *out,uint32_t n){
  uint32_t offset;
  if(!rt || !rt->memory || address<rt->base)return 0;
  offset=address-rt->base;
  if(offset>rt->used || n>rt->used-offset)return 0;
  memcpy(out,rt->memory+offset,n);return 1;
}
void dc_goal_art_probe(dc_goal_runtime *rt,uint32_t step){
  volatile dc_goal_art_probe_row *row;uint32_t symbol=0,w=0xffffffffu,i;uint16_t half=0xffffu;
  if(dc_goal_art_symbol_probe.count>=8)return;
  row=&dc_goal_art_symbol_probe.row[dc_goal_art_symbol_probe.count++];
  row->step=step;row->used=rt?rt->used:0;row->symbol_type=rt?rt->symbol_type:0;
  row->symbol=row->type_word=row->prev_value=row->next_type=0xffffffffu;
  row->symbol_type_tag=0xffffffffu;row->symbol_type_methods=0xffffu;
  for(i=0;i<6u;++i)row->symbol_type_header[i]=0xffffffffu;
  if(rt && probe_mem(rt,rt->code_registry_head+8u,&symbol,4))row->symbol=symbol;
  if(symbol>16u){
    if(probe_mem(rt,symbol-4u,&w,4))row->type_word=w;
    if(probe_mem(rt,symbol-8u,&w,4))row->prev_value=w;
    if(probe_mem(rt,symbol+4u,&w,4))row->next_type=w;
  }
  if(row->symbol_type>16u){
    if(probe_mem(rt,row->symbol_type-4u,&w,4))row->symbol_type_tag=w;
    if(probe_mem(rt,row->symbol_type+14u,&half,2))row->symbol_type_methods=half;
    for(i=0;i<6u;++i)
      if(probe_mem(rt,row->symbol_type+i*4u,&w,4))row->symbol_type_header[i]=w;
  }
}
#define ART_PROBE(n) dc_goal_art_probe(rt,(n))
#else
#define ART_PROBE(n) ((void)0)
#endif
static uint32_t word(const unsigned char *p){
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int fail(dc_goal_runtime *rt,enum dc_goal_error error){
  if(rt && rt->error==DC_GOAL_OK)rt->error=error;
  return 0;
}
static int read_at(FILE *f,uint32_t at,void *out,uint32_t bytes){
  return !fseek(f,(long)at,SEEK_SET) && fread(out,1,bytes,f)==bytes;
}
static int name_is(const unsigned char *p,const char *s){
  size_t n=strlen(s),i;
  if(n>=60 || memcmp(p,s,n) || p[n])return 0;
  for(i=n+1;i<60;++i)if(p[i])return 0;
  return 1;
}
int dc_goal_art_admit(dc_goal_runtime *rt,uint32_t bytes,dc_goal_art_result *r){
  if(!rt || !r)return fail(rt,DC_GOAL_BAD_ARGUMENT);
  memset(r,0,sizeof *r);r->before=rt->used;r->capacity=rt->capacity;
  r->temporary=rt->temporary;r->payload=bytes;
  if(rt->error!=DC_GOAL_OK)return 0;
  if(!bytes || (bytes&15u) || rt->temporary>rt->capacity || rt->used>rt->capacity-rt->temporary)
    return fail(rt,DC_GOAL_BAD_ARGUMENT);
  r->available=rt->capacity-rt->temporary-rt->used;
  if(bytes>r->available){
    r->shortfall=bytes-r->available;
    /* Execute the real allocator refusal. It leaves used/memory unchanged. */
    (void)dc_goal_alloc(rt,bytes);
    return 0;
  }
  return 1;
}
static int site(FILE *f,const art_row *row,const unsigned char *data,uint32_t at,
                uint32_t *value){
  unsigned char b[4];
  if((at&3u) || at>row->size-4)return 0;
  if(data)*value=word(data+at);
  else {if(!read_at(f,row->data+at,b,4))return 0;*value=word(b);}
  return 1;
}
static void put(unsigned char *data,uint32_t at,uint32_t value){memcpy(data+at,&value,4);}
/* Validation pass uses source words through FILE; the apply pass uses the
 * arena payload. The compact original link table is the only malloc buffer.
 * All types must already be defined by native GAME.CGO. New resource symbols
 * are interned only in the apply pass, with the normal native symbol identity. */
/* forward: a type binding to a type no module has defined yet interns a forward
 * type with the binding's method count, as the original linker's intern_type
 * does (level data names actor types its own code defines later). Otherwise all
 * types must already be defined by native GAME.CGO. */
static int links(dc_goal_runtime *rt,FILE *f,const art_row *row,unsigned char *data,
                 uint32_t base,dc_goal_art_result *result,const char *root_type,int forward,
                 const unsigned char *source){
  unsigned char *table=malloc(row->length);
  uint32_t p=12,at=0,value,count,fixing=0,root=0;
  int ok=0;
  if(!table)return fail(rt,DC_GOAL_OUT_OF_MEMORY);
  if(!read_at(f,row->link,table,row->length) || word(table)!=UINT32_MAX ||
     word(table+4)!=row->length || word(table+8)!=2)goto done;
  if(table[p]==0)++p;
  else {
    for(;;){
      for(;;){
        if(p>=row->length)goto done;
        count=table[p++];
        if(at>row->size || count>(row->size-at)/4)goto done;
        if(fixing){
          uint32_t i;
          for(i=0;i<count;++i){
            if(!site(f,row,data?data:source,at,&value) || value>=row->size)goto done;
            if(data){put(data,at,base+value);++result->pointers;}
            at+=4;
          }
        }else at+=count*4;
        if(count!=255)break;
        if(p>=row->length)goto done;
        if(!table[p]){++p;fixing^=1;}
      }
      fixing^=1;
      if(p>=row->length)goto done;
      if(!table[p]){++p;break;}
    }
  }
  while(p<row->length && table[p]){
    uint32_t kind=table[p++],is_type=kind&128u,target=0;
    const char *name;
    unsigned char *end;
    if(!is_type){if(kind<=9)goto done;--p;}
    end=memchr(table+p,0,row->length-p);
    if(!end || end==table+p || (size_t)(end-table-p)>255)goto done;
    name=(const char *)table+p;p=(uint32_t)(end-table)+1;at=0;
    if(data){
      uint32_t symbol=dc_goal_intern(rt,name);
      if(!symbol || rt->error!=DC_GOAL_OK)goto done;
      target=is_type?dc_goal_symbol_value(rt,symbol):symbol;
      if(is_type && forward && (!target || target==rt->s7))target=dc_goal_intern_type(rt,name,(uint16_t)(kind&127u));
      if(is_type && (!target || target==rt->s7 || dc_goal_type_of(rt,target)!=rt->type_type))goto done;
    }
    for(;;){
      uint32_t delta,step=1;
      if(p>=row->length)goto done;
      delta=table[p];
      if(delta&3u){
        if(p+1>=row->length)goto done;
        delta|=(uint32_t)table[p+1]<<8;step=2;
        if(delta&2u){
          if(p+2>=row->length)goto done;
          delta|=(uint32_t)table[p+2]<<16;step=3;
          if(delta&1u){
            if(p+3>=row->length)goto done;
            delta|=(uint32_t)table[p+3]<<24;step=4;
          }
        }
      }
      p+=step;delta&=~3u;
      if(delta>row->size || at>row->size-delta)goto done;
      at+=delta;
      if(!site(f,row,data?data:source,at,&value) || value!=UINT32_MAX)goto done;
      if(is_type && root_type && !strcmp(name,root_type) && !at)root=1;
      if(data){put(data,at,target);++result->bindings;}
      if(p>=row->length)goto done;
      if(!table[p]){++p;break;}
    }
  }
  /* A NULL root_type accepts any root (the DGO server links whatever object comes). */
  if(p>=row->length || (root_type && !root))goto done;
  while(p<row->length)if(table[p++])goto done;
  ok=1;
done:
  free(table);
  return ok?1:fail(rt,DC_GOAL_BAD_MODULE);
}
/* One v4 data object of a DGO, linked in place at a guest address (the level
 * heap). object_at is the file offset of its 16-byte v4 header, object_bytes its
 * DGO size. Any root type; forward types interned. *data_bytes is the linked
 * data's size: the object occupies [dest, dest+*data_bytes). Runs no GOAL code. */
int dc_goal_link_v4_at(dc_goal_runtime *rt,FILE *f,uint32_t object_at,uint32_t object_bytes,
                       uint32_t dest,uint32_t *data_bytes){
  unsigned char v4[16],*data;art_row row;dc_goal_art_result result;uint32_t tail,end;
  if(data_bytes)*data_bytes=0;
  if(!rt || !f || !data_bytes || (dest&15u))return fail(rt,DC_GOAL_BAD_ARGUMENT);
  if(rt->error!=DC_GOAL_OK)return 0;
  memset(&result,0,sizeof result);
  if(object_bytes<64 || (object_bytes&15u) || !read_at(f,object_at,v4,16))return fail(rt,DC_GOAL_BAD_MODULE);
  row=(art_row){object_at+16u,word(v4+12),0,word(v4+4)};
  if(word(v4)!=UINT32_MAX || word(v4+8)!=4 || row.size<32 || (row.size&15u) ||
     row.length<14 || row.length>1024u*1024u || row.size>object_bytes-16u ||
     row.length>object_bytes-16u-row.size)return fail(rt,DC_GOAL_BAD_MODULE);
  row.link=row.data+row.size;tail=row.link+row.length;end=object_at+object_bytes;
  while(tail<end){unsigned char b;if(!read_at(f,tail++,&b,1) || b)return fail(rt,DC_GOAL_BAD_MODULE);}
  /* The data is read once, into dest (heap space nothing owns until the link
   * succeeds); the validation pass reads its pointer words there instead of a
   * disc seek per word, and only the apply pass interns symbols. */
  data=dc_goal_resolve(rt,dest,row.size);
  if(!data || !read_at(f,row.data,data,row.size))return fail(rt,DC_GOAL_BAD_MODULE);
  if(!links(rt,f,&row,NULL,0,&result,NULL,1,data))return 0;
  if(!links(rt,f,&row,data,dest,&result,NULL,1,NULL))return fail(rt,DC_GOAL_BAD_MODULE);
  *data_bytes=row.size;
  return 1;
}
int dc_goal_load_v4_single(dc_goal_runtime *rt,FILE *f,const char *dgo_name,const char *object_name,
                           const char *root_type,uint32_t *root,dc_goal_art_result *result){
  unsigned char h[64],v4[16],*data;art_row row;uint32_t size,base,tail,at;
  if(result)memset(result,0,sizeof *result);
  if(root)*root=0;
  if(!rt || !f || !dgo_name || !object_name || !root_type || !root || !result)return fail(rt,DC_GOAL_BAD_ARGUMENT);
  if(rt->error!=DC_GOAL_OK)return 0;
  if(!read_at(f,0,h,64) || word(h)!=1 || !name_is(h+4,dgo_name))return fail(rt,DC_GOAL_BAD_MODULE);
  if(!read_at(f,64,h,64) || !name_is(h+4,object_name))return fail(rt,DC_GOAL_BAD_MODULE);
  size=word(h);
  if(size<64 || size>8u*1024u*1024u || (size&15u) || !read_at(f,128,v4,16))return fail(rt,DC_GOAL_BAD_MODULE);
  row=(art_row){144,word(v4+12),0,word(v4+4)};
  if(word(v4)!=UINT32_MAX || word(v4+8)!=4 || row.size<32 || (row.size&15u) ||
     row.length<14 || row.length>1024u*1024u || row.size>size-16u ||
     row.length>size-16u-row.size)return fail(rt,DC_GOAL_BAD_MODULE);
  row.link=row.data+row.size;tail=row.link+row.length;at=128u+size;
  while(tail<at){unsigned char b;if(!read_at(f,tail++,&b,1) || b)return fail(rt,DC_GOAL_BAD_MODULE);}
  if(fseek(f,(long)at,SEEK_SET) || fgetc(f)!=EOF || ferror(f))return fail(rt,DC_GOAL_BAD_MODULE);
  if(!dc_goal_art_admit(rt,row.size,result))return 0;
  result->objects=1;
  if(!links(rt,f,&row,NULL,0,result,root_type,1,NULL))return 0;
  base=dc_goal_alloc(rt,row.size);if(!base)return 0;
  data=dc_goal_resolve(rt,base,row.size);
  if(!data || !read_at(f,row.data,data,row.size) || !links(rt,f,&row,data,base,result,root_type,1,NULL))
    return fail(rt,DC_GOAL_BAD_MODULE);
  result->linked=1;result->art[0]=base+4;*root=base+4;
  return 1;
}
int dc_goal_load_art_dgo(dc_goal_runtime *rt,FILE *f,uint32_t expected_payload,dc_goal_art_relocate relocate,
                       void *context,dc_goal_art_result *result){
  static const char *const names[]={"eichar","sidekick"};
  unsigned char h[64],v4[16];art_row rows[2];
  uint32_t i,at=64,total=0,base;
#ifdef DC_ARENA_JAK_PLAYER
  uint32_t count,resident=0;unsigned char placed[2]={0,0};
#endif
  dc_goal_art_result admitted;
  if(result)memset(result,0,sizeof *result);
  if(!rt || !f || !result || !relocate)return fail(rt,DC_GOAL_BAD_ARGUMENT);
  if(rt->error!=DC_GOAL_OK)return 0;
  ART_PROBE(1);
#ifdef DC_ARENA_JAK_PLAYER
  if(!read_at(f,0,h,64))return fail(rt,DC_GOAL_BAD_MODULE);
  count=word(h);
  if(!((count==1 && name_is(h+4,"EICHAR.DGO")) ||
       (count==2 && name_is(h+4,"JAKART.DGO"))))return fail(rt,DC_GOAL_BAD_MODULE);
  for(i=0;i<count;++i){
#else
  if(!read_at(f,0,h,64) || word(h)!=2 || !name_is(h+4,"JAKART.DGO"))return fail(rt,DC_GOAL_BAD_MODULE);
  for(i=0;i<2;++i){
#endif
    uint32_t size;
    if(!read_at(f,at,h,64) || !name_is(h+4,names[i]))return fail(rt,DC_GOAL_BAD_MODULE);
    size=word(h);
    if(size<64 || size>4u*1024u*1024u || (size&15u) || !read_at(f,at+64,v4,16))return fail(rt,DC_GOAL_BAD_MODULE);
    rows[i]=(art_row){at+80,word(v4+12),0,word(v4+4)};
    if(word(v4)!=UINT32_MAX || word(v4+8)!=4 || rows[i].size<32 || (rows[i].size&15u) ||
       rows[i].length<14 || rows[i].length>65536 || rows[i].size>size-16 ||
       rows[i].length>size-16-rows[i].size)return fail(rt,DC_GOAL_BAD_MODULE);
    rows[i].link=rows[i].data+rows[i].size;total+=rows[i].size;at+=64+size;
    /* Data files are padded to 16, all padding must be zero. */
    {uint32_t tail=rows[i].link+rows[i].length;
     while(tail<at){unsigned char b;if(!read_at(f,tail++,&b,1) || b)return fail(rt,DC_GOAL_BAD_MODULE);}}
  }
  if(fseek(f,(long)at,SEEK_SET) || fgetc(f)!=EOF || ferror(f))return fail(rt,DC_GOAL_BAD_MODULE);
  if(total!=expected_payload)return fail(rt,DC_GOAL_BAD_MODULE);
#ifdef DC_ARENA_JAK_PLAYER
  /* Objects that do not fit the arena, in order, go to the resident main-RAM
   * region when one is registered. Each object relocates against its own base.
   * Admit every placement before reading link tables or modifying symbols. */
  {uint32_t arena_total=0,resident_total=0,available;
   if(rt->temporary>rt->capacity || rt->used>rt->capacity-rt->temporary)return fail(rt,DC_GOAL_BAD_ARGUMENT);
   available=rt->capacity-rt->temporary-rt->used;
   for(i=0;i<count;++i){
     if(!resident_total && rows[i].size<=available-arena_total){placed[i]=0;arena_total+=rows[i].size;}
     else {placed[i]=1;resident_total+=rows[i].size;}
   }
   if(resident_total && (!rt->resident_memory || rt->resident_used>rt->resident_capacity ||
                         resident_total>rt->resident_capacity-rt->resident_used)){
     /* No room anywhere: the arena refusal records the whole shortfall. */
     if(!dc_goal_art_admit(rt,total,&admitted)){*result=admitted;return 0;}
   }
   if(arena_total && !dc_goal_art_admit(rt,arena_total,&admitted)){*result=admitted;return 0;}
   if(!arena_total){memset(&admitted,0,sizeof admitted);admitted.before=rt->used;admitted.capacity=rt->capacity;}
   admitted.payload=total;
   *result=admitted;result->objects=count;
   for(i=0;i<count;++i)if(!links(rt,f,&rows[i],NULL,0,result,"art-group",0,NULL))return 0;
   ART_PROBE(2);
   base=arena_total?dc_goal_alloc(rt,arena_total):0;
   resident=resident_total?dc_goal_alloc_resident(rt,resident_total):0;
   if((arena_total && !base) || (resident_total && !resident))return 0;
  }
  ART_PROBE(3);
  for(i=0;i<count;++i){
    uint32_t *at_base=placed[i]?&resident:&base;
    unsigned char *data=dc_goal_resolve(rt,*at_base,rows[i].size);
    if(!data || !read_at(f,rows[i].data,data,rows[i].size) || !links(rt,f,&rows[i],data,*at_base,result,"art-group",0,NULL))
      return fail(rt,DC_GOAL_BAD_MODULE);
    result->art[i]=*at_base+4;++result->linked;*at_base+=rows[i].size;
  }
#else
  /* Admit the entire pair before reading link tables or modifying symbols. */
  if(!dc_goal_art_admit(rt,total,&admitted)){*result=admitted;return 0;}
  *result=admitted;result->objects=2;
  for(i=0;i<2;++i)if(!links(rt,f,&rows[i],NULL,0,result,"art-group",0,NULL))return 0;
  ART_PROBE(2);
  base=dc_goal_alloc(rt,total);if(!base)return 0;
  ART_PROBE(3);
  for(i=0;i<2;++i){
    unsigned char *data=dc_goal_resolve(rt,base,rows[i].size);
    if(!data || !read_at(f,rows[i].data,data,rows[i].size) || !links(rt,f,&rows[i],data,base,result,"art-group",0,NULL))
      return fail(rt,DC_GOAL_BAD_MODULE);
    result->art[i]=base+4;++result->linked;base+=rows[i].size;
  }
#endif
  ART_PROBE(4);
#ifdef DC_ARENA_JAK_PLAYER
  for(i=0;i<count;++i){
#else
  for(i=0;i<2;++i){
#endif
    if(!relocate(rt,result->art[i],context) || rt->error!=DC_GOAL_OK)return fail(rt,DC_GOAL_BAD_FUNCTION);
    ++result->relocated;
  }
  ART_PROBE(5);
  return 1;
}
#ifdef DC_ARENA_JAK_PLAYER
/* One retail v4 dir-tpages object. The callback must invoke original
 * texture-page-dir.relocate, which only stores this in *texture-page-dir*.
 * The DGO size includes the zero pad to 16 bytes, so EOF matches the art loader. */
int dc_goal_load_tpage_dir(dc_goal_runtime *rt,FILE *f,uint32_t expected_payload,
                           dc_goal_art_relocate relocate,void *context){
  unsigned char h[64],v4[16];
  art_row row;
  dc_goal_art_result result;
  uint32_t size,base,total,tail,at;
  unsigned char *data;
  if(!rt || !f || !relocate || !expected_payload)return fail(rt,DC_GOAL_BAD_ARGUMENT);
  if(rt->error!=DC_GOAL_OK)return 0;
  if(!read_at(f,0,h,64))return fail(rt,DC_GOAL_BAD_MODULE);
  if(word(h)!=1 || !name_is(h+4,"DIR-TPAGES.DGO"))return fail(rt,DC_GOAL_BAD_MODULE);
  if(!read_at(f,64,h,64) || !name_is(h+4,"dir-tpages"))return fail(rt,DC_GOAL_BAD_MODULE);
  size=word(h);
  if(size<64 || size>1024u*1024u || (size&15u) || !read_at(f,128,v4,16))return fail(rt,DC_GOAL_BAD_MODULE);
  row=(art_row){144,word(v4+12),0,word(v4+4)};
  if(word(v4)!=UINT32_MAX || word(v4+8)!=4 || row.size<32 || (row.size&15u) ||
     row.length<14 || row.length>65536u || row.size>size-16u ||
     row.length>size-16u-row.size)return fail(rt,DC_GOAL_BAD_MODULE);
  row.link=row.data+row.size;
  total=row.size;
  tail=row.link+row.length;
  at=128u+size;
  while(tail<at){unsigned char b;if(!read_at(f,tail++,&b,1) || b)return fail(rt,DC_GOAL_BAD_MODULE);}
  if(fseek(f,(long)at,SEEK_SET) || fgetc(f)!=EOF || ferror(f))return fail(rt,DC_GOAL_BAD_MODULE);
  if(total!=expected_payload)return fail(rt,DC_GOAL_BAD_MODULE);
  if(!dc_goal_art_admit(rt,total,&result))return 0;
  if(!links(rt,f,&row,NULL,0,&result,"texture-page-dir",0,NULL))return 0;
  base=dc_goal_alloc(rt,total);if(!base)return 0;
  data=dc_goal_resolve(rt,base,row.size);
  if(!data || !read_at(f,row.data,data,row.size) ||
     !links(rt,f,&row,data,base,&result,"texture-page-dir",0,NULL))
    return fail(rt,DC_GOAL_BAD_MODULE);
  if(!relocate(rt,base+4,context) || rt->error!=DC_GOAL_OK)return fail(rt,DC_GOAL_BAD_FUNCTION);
  return 1;
}
#endif
