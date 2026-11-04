/* Native counterpart of Jak 1 work_v2 + c_symlink2, data only (AI-assisted), ISC. */
#include "goal_art.h"
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t data,size,link,length;} art_row;
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
static int site(FILE *f,const art_row *row,unsigned char *data,uint32_t at,
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
static int links(dc_goal_runtime *rt,FILE *f,const art_row *row,unsigned char *data,
                 uint32_t base,dc_goal_art_result *result){
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
            if(!site(f,row,data,at,&value) || value>=row->size)goto done;
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
      if(!site(f,row,data,at,&value) || value!=UINT32_MAX)goto done;
      if(is_type && !strcmp(name,"art-group") && !at)root=1;
      if(data){put(data,at,target);++result->bindings;}
      if(p>=row->length)goto done;
      if(!table[p]){++p;break;}
    }
  }
  if(p>=row->length || !root)goto done;
  while(p<row->length)if(table[p++])goto done;
  ok=1;
done:
  free(table);
  return ok?1:fail(rt,DC_GOAL_BAD_MODULE);
}
int dc_goal_load_art_dgo(dc_goal_runtime *rt,FILE *f,uint32_t expected_payload,dc_goal_art_relocate relocate,
                       void *context,dc_goal_art_result *result){
  static const char *const names[]={"eichar","sidekick"};
  unsigned char h[64],v4[16];art_row rows[2];
  uint32_t i,at=64,total=0,base;
#ifdef DC_ARENA_JAK_PLAYER
  uint32_t count;
#endif
  dc_goal_art_result admitted;
  if(result)memset(result,0,sizeof *result);
  if(!rt || !f || !result || !relocate)return fail(rt,DC_GOAL_BAD_ARGUMENT);
  if(rt->error!=DC_GOAL_OK)return 0;
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
  /* Admit the entire pair before reading link tables or modifying symbols. */
  if(!dc_goal_art_admit(rt,total,&admitted)){*result=admitted;return 0;}
#ifdef DC_ARENA_JAK_PLAYER
  *result=admitted;result->objects=count;
  for(i=0;i<count;++i)if(!links(rt,f,&rows[i],NULL,0,result))return 0;
#else
  *result=admitted;result->objects=2;
  for(i=0;i<2;++i)if(!links(rt,f,&rows[i],NULL,0,result))return 0;
#endif
  base=dc_goal_alloc(rt,total);if(!base)return 0;
#ifdef DC_ARENA_JAK_PLAYER
  for(i=0;i<count;++i){
#else
  for(i=0;i<2;++i){
#endif
    unsigned char *data=dc_goal_resolve(rt,base,rows[i].size);
    if(!data || !read_at(f,rows[i].data,data,rows[i].size) || !links(rt,f,&rows[i],data,base,result))
      return fail(rt,DC_GOAL_BAD_MODULE);
    result->art[i]=base+4;++result->linked;base+=rows[i].size;
  }
#ifdef DC_ARENA_JAK_PLAYER
  for(i=0;i<count;++i){
#else
  for(i=0;i<2;++i){
#endif
    if(!relocate(rt,result->art[i],context) || rt->error!=DC_GOAL_OK)return fail(rt,DC_GOAL_BAD_FUNCTION);
    ++result->relocated;
  }
  return 1;
}
