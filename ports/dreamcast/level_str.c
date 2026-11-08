/* Streamed (spooled) animation chunks for the level ELF (AI-assisted), ISC.
 *
 * The IOP's STR server (overlord stream.cpp RPC_STR, rpc-call port 4) and the C
 * kernel's link (klink.cpp, retail v2 objects), which original
 * external-art-buffer uses to load a spooled animation one chunk at a time:
 * str-load asks for chunk N of an animation at the buffer's heap, str-load-status
 * reports its length, and link turns the chunk into an art-group the buffer logs
 * in.
 *
 * Files: /cd/STR/<ISO name>.STR, the ISO name made from the animation name as
 * file_util::ISONameFromAnimationName does (eichar-ambient-1 -> EIA1). The first
 * 2048-byte sector holds 64 chunk start sectors then 64 chunk byte sizes.
 * A chunk is a retail v2 object: {0xffffffff, link block length, 2}, the pointer
 * table at +12, the symbol table after it, then the data at the link block's
 * length. A chunk id of -1 loads a whole file by name (/cd, then /cd/TEXT).
 * --dump dc_level_str_evidence 304 */
#include "level_str.h"
#include "goal_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

enum {STR_RESULT_DONE=0,STR_RESULT_ERROR=1,STR_CHUNKS=64,STR_SECTOR=2048};
/* The two spool buffers: original external-art-buffer update puts each one's
 * heap at #x84000 + index * SPOOL_HEAP_SIZE (247 KiB), PS2 kernel memory the
 * game reuses. That is not RAM on the Dreamcast; the first request into it moves
 * both heaps to SPOOL_BYTES of KOS memory (the runtime's resident region) and is
 * answered error, which the buffer drops and asks again into its new heap. A
 * chunk larger than SPOOL_BYTES is refused the same way. */
enum {PS2_SPOOL_BASE=0x84000,PS2_SPOOL_HEAP=247*1024,SPOOL_BYTES=128*1024};
volatile struct {
  uint32_t requests,served,errors,missing,links,linked,link_errors,last_chunk;
  uint32_t last_bytes,last_dest,last_root,symbols,types,pointers,files,last_error,moved,chunk_requests,chunk_served,pad;
  char last_name[32];
  char names[8][24];  /* the distinct names asked for, chunk ones marked with a leading '#' */
} dc_level_str_evidence;

static dc_goal_runtime *runtime;
static FILE *str_file;
static char str_path[32];
static uint32_t str_sectors[STR_CHUNKS],str_sizes[STR_CHUNKS];

static uint32_t rd32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t spool_base;
/* The spool heaps' KOS block, taken at boot (dc_level_str_reserve). */
static void *spool_memory;
void dc_level_str_reserve(void){if(!spool_memory)spool_memory=memalign(64,2u*SPOOL_BYTES);}
static uint32_t named(const char *name){
  enum dc_goal_error prior=runtime->error;uint32_t s=dc_goal_find(runtime,name),v=s?dc_goal_symbol_value(runtime,s):0;
  runtime->error=prior;return v;
}
/* *art-control* buffer i (+4i) -> its kheap (+28): base, top, current, top-base. */
static int move_spool_heaps(void){
  uint32_t control=named("*art-control*"),i;unsigned char *buffer,*heap;
  if(!spool_base){
    void *memory=spool_memory?spool_memory:memalign(64,2u*SPOOL_BYTES);uint32_t base,at;
    spool_memory=NULL;
    if(!memory)return 0;
    base=(uint32_t)(uintptr_t)memory&0x1fffffffu;  /* the cached P0 alias, as GOAL addresses are */
    if(!dc_goal_register_resident(runtime,memory,base,2u*SPOOL_BYTES) ||
       (at=dc_goal_alloc_resident(runtime,2u*SPOOL_BYTES))!=base){free(memory);runtime->error=DC_GOAL_OK;return 0;}
    spool_base=base;
  }
  if(!control || control==runtime->s7)return 0;
  for(i=0;i<2;++i){
    uint32_t b=0,base=spool_base+i*SPOOL_BYTES,top=base+SPOOL_BYTES;
    if(!(buffer=dc_goal_resolve(runtime,control+4u*i,4)))return 0;
    memcpy(&b,buffer,4);
    if(!b || b==runtime->s7 || !(heap=dc_goal_resolve(runtime,b+28u,16)))return 0;
    memcpy(heap,&base,4);memcpy(heap+4,&top,4);memcpy(heap+8,&base,4);memcpy(heap+12,&top,4);
  }
  ++dc_level_str_evidence.moved;
  return 1;
}
static size_t bounded_len(const char *s,size_t max){size_t n=0;while(n<max && s[n])++n;return n;}

/* file_util::ISONameFromAnimationName: two letters of the first word, then one
 * character a later word (its trailing digit, or a special letter, or its first
 * letter), then the rest of the last word, eight in all, upper case. */
static void iso_name(char dst[9],const char *src){
  const char *at=src,*next;int i=2;
  dst[0]=src[0];dst[1]=src[0]?src[1]:0;
  while(*at && *at!='-')++at;
  next=at;
  if(*at){
    while(at=next+1,i<8){
      char c;
      next=at;while(*next && *next!='-')++next;
      if(!*next)break;
      if(next[-1]<'0' || next[-1]>'9'){
        if(next-at==10 && !memcmp(at,"resolution",10))c='z';
        else if(next-at==6 && !memcmp(at,"accept",6))c='y';
        else if(next-at==6 && !memcmp(at,"reject",6))c='n';
        else if(next-at==5 && !memcmp(at,"keira",5))c='i';
        else c=*at;
      } else c=next[-1];
      dst[i++]=c;
    }
    while(*at && i<8)dst[i++]=*at++;
  }
  while(i<8)dst[i++]=' ';
  for(i=0;i<8;++i)if(dst[i]>='a' && dst[i]<='z')dst[i]=(char)(dst[i]-32);
  dst[8]=0;
}
static int open_str(const char *animation){
  char name[9],path[32];int n;unsigned char header[STR_SECTOR/4];unsigned i;
  iso_name(name,animation);
  for(n=7;n>=0 && name[n]==' ';--n)name[n]=0;
  snprintf(path,sizeof path,"/cd/STR/%s.STR",name);
  if(str_file && !strcmp(path,str_path))return 1;
  if(str_file){fclose(str_file);str_file=NULL;}
  str_path[0]=0;
  if(!(str_file=fopen(path,"rb")))return 0;
  if(fread(header,1,sizeof header,str_file)!=sizeof header){fclose(str_file);str_file=NULL;return 0;}
  for(i=0;i<STR_CHUNKS;++i){str_sectors[i]=rd32(header+4*i);str_sizes[i]=rd32(header+256+4*i);}
  strcpy(str_path,path);
  return 1;
}

/* RPC_STR for a chunk (chunk-id >= 0): load-chunk-msg is u16 rsvd, u16 result,
 * u32 address, s32 chunk, u32 length (the most to load in, the loaded length
 * out), char name[64]. Loading is synchronous, so the next rpc-busy? is 0. */
static uint64_t str_rpc(uint64_t port,uint64_t fno,uint64_t async,uint64_t send,
                        uint64_t send_bytes,uint64_t recv,uint64_t recv_bytes){
  unsigned char *msg,*reply,*dest=NULL;uint32_t address,maxlen,size;int32_t chunk;char name[65];unsigned i;
  uint16_t result=STR_RESULT_ERROR;
  (void)port;(void)fno;(void)async;(void)send_bytes;
  if(!runtime || send>UINT32_MAX || recv>UINT32_MAX || recv_bytes<16 ||
     !(msg=dc_goal_resolve(runtime,(uint32_t)send,80)) || !(reply=dc_goal_resolve(runtime,(uint32_t)recv,16))){
    if(runtime)runtime->error=DC_GOAL_OK;
    return 0;
  }
  ++dc_level_str_evidence.requests;
  address=rd32(msg+4);memcpy(&chunk,msg+8,4);maxlen=rd32(msg+12);
  for(i=0;i<64 && msg[16+i];++i)name[i]=(char)msg[16+i];
  name[i]=0;
  for(i=0;i<31 && name[i];++i)dc_level_str_evidence.last_name[i]=name[i];
  dc_level_str_evidence.last_name[i]=0;
  dc_level_str_evidence.last_chunk=(uint32_t)chunk;
  {unsigned k,j;char tag[24];
   tag[0]=chunk<0?' ':'#';for(j=0;j<22 && name[j];++j)tag[j+1]=name[j];tag[j+1]=0;
   for(k=0;k<8 && dc_level_str_evidence.names[k][0];++k)if(!strncmp((const char *)dc_level_str_evidence.names[k],tag,24))break;
   if(k<8 && !dc_level_str_evidence.names[k][0])for(j=0;j<24;++j)dc_level_str_evidence.names[k][j]=tag[j];}
  if(chunk<0){
    /* a whole file by name, at most length bytes */
    static const char *const dirs[2]={"/cd/","/cd/TEXT/"};char path[96];FILE *f=NULL;long bytes;unsigned d,j;
    for(d=0;d<2 && !f;++d){
      snprintf(path,sizeof path,"%s%s",dirs[d],name);
      for(j=0;path[j];++j)if(path[j]>='a' && path[j]<='z')path[j]=(char)(path[j]-32);
      f=fopen(path,"rb");
    }
    if(!f){++dc_level_str_evidence.missing;goto reply;}
    if(fseek(f,0,SEEK_END) || (bytes=ftell(f))<=0 || (uint32_t)bytes>maxlen || fseek(f,0,SEEK_SET) ||
       !(dest=dc_goal_resolve(runtime,address,(uint32_t)bytes)) || fread(dest,1,(size_t)bytes,f)!=(size_t)bytes){
      fclose(f);dc_level_str_evidence.last_error=1;++dc_level_str_evidence.errors;runtime->error=DC_GOAL_OK;goto reply;}
    fclose(f);size=(uint32_t)bytes;++dc_level_str_evidence.files;
  } else {
    if(address>=PS2_SPOOL_BASE && address<PS2_SPOOL_BASE+2u*PS2_SPOOL_HEAP){
      dc_level_str_evidence.last_error=7;if(!move_spool_heaps())runtime->error=DC_GOAL_OK;
      ++dc_level_str_evidence.errors;goto reply;}
    ++dc_level_str_evidence.chunk_requests;
    if(chunk>=STR_CHUNKS || !open_str(name)){++dc_level_str_evidence.missing;goto reply;}
    size=str_sizes[chunk];
    dc_level_str_evidence.last_error=!size?2:size>maxlen?3:(address&15u)?4:0;
    if(!dc_level_str_evidence.last_error && !(dest=dc_goal_resolve(runtime,address,size)))dc_level_str_evidence.last_error=5;
    if(!dc_level_str_evidence.last_error && (fseek(str_file,(long)str_sectors[chunk]*STR_SECTOR,SEEK_SET) ||
       fread(dest,1,size,str_file)!=size))dc_level_str_evidence.last_error=6;
    if(dc_level_str_evidence.last_error){++dc_level_str_evidence.errors;runtime->error=DC_GOAL_OK;goto reply;}
  }
  memcpy(msg+12,&size,4);
  if(chunk>=0)++dc_level_str_evidence.chunk_served;
  ++dc_level_str_evidence.served;dc_level_str_evidence.last_bytes=size;dc_level_str_evidence.last_dest=address;
  result=STR_RESULT_DONE;
reply:
  memcpy(reply+2,&result,2);
  if(reply!=msg)memcpy(reply+12,msg+12,4);
  return 0;
}

/* klink.cpp jak1_work_v2: the data moves to the heap's current (the link block
 * is copied aside first, as the kernel moves it to the heap top), the heap's
 * current is the data's end, the pointer table adds the data's address to the
 * words it names, and the symbol table interns each symbol or type and stores it
 * in every word of the data that holds 0xffffffff at its offsets. */
static int bad_link(void){++dc_level_str_evidence.link_errors;if(runtime->error==DC_GOAL_OK)runtime->error=DC_GOAL_BAD_MODULE;return 0;}
static uint64_t link_v2(uint64_t data,uint64_t name,uint64_t length,uint64_t heap,uint64_t flags){
  unsigned char *object,*kheap,*block=NULL,*dest,*end;uint32_t link_bytes,code_bytes,current,top,at,base;
  const unsigned char *r;
  (void)name;(void)flags;
  if(!runtime || data>UINT32_MAX || length>UINT32_MAX || heap>UINT32_MAX || runtime->error!=DC_GOAL_OK)return 0;
  ++dc_level_str_evidence.links;
  if(length<16 || !(object=dc_goal_resolve(runtime,(uint32_t)data,(uint32_t)length)) ||
     !(kheap=dc_goal_resolve(runtime,(uint32_t)heap,16)))return bad_link();
  link_bytes=rd32(object+4);
  if(rd32(object)!=UINT32_MAX || rd32(object+8)!=2 || link_bytes<13 || link_bytes>=length)return bad_link();
  code_bytes=(uint32_t)length-link_bytes;
  current=(rd32(kheap+8)+15u)&~15u;top=rd32(kheap+4);
  if(current>(uint32_t)data || code_bytes>top-current)return bad_link();
  if(!(block=malloc(link_bytes)))return bad_link();
  memcpy(block,object,link_bytes);
  if(!(dest=dc_goal_resolve(runtime,current,code_bytes))){free(block);return bad_link();}
  memmove(dest,object+link_bytes,code_bytes);
  base=current;end=dest+code_bytes;
  {uint32_t next=(current+code_bytes+15u)&~15u;memcpy(kheap+8,&next,4);}
  /* pointers: alternating seek and fix counts of words, 0xff continuing a run */
  r=block+12;
  if(*r==0)++r;
  else {
    unsigned char *loc=dest;int fixing=0;
    for(;;){
      for(;;){
        unsigned count;
        if(r>=block+link_bytes){free(block);return bad_link();}
        count=*r++;
        if(!fixing)loc+=4u*count;
        else {unsigned i;
          for(i=0;i<count;++i){uint32_t w;
            if(loc+4>end){free(block);return bad_link();}
            memcpy(&w,loc,4);w+=base;memcpy(loc,&w,4);loc+=4;++dc_level_str_evidence.pointers;}}
        if(count!=0xff)break;
        if(*r==0){++r;fixing^=1;}
      }
      fixing^=1;
      if(*r==0)break;
    }
    ++r;
  }
  /* symbols and types */
  while(r<block+link_bytes && *r){
    uint32_t relocation=*r++,object_ref;const char *text;size_t n;unsigned char *obj=dest;
    if(!(relocation&0x80)){
      if(relocation>9)--r;
      text=(const char *)r;n=bounded_len(text,(size_t)(block+link_bytes-r));
      if(r+n>=block+link_bytes){free(block);return bad_link();}
      object_ref=dc_goal_intern(runtime,text);++dc_level_str_evidence.symbols;
    } else {
      unsigned methods=relocation&0x7fu;
      text=(const char *)r;n=bounded_len(text,(size_t)(block+link_bytes-r));
      if(r+n>=block+link_bytes){free(block);return bad_link();}
      object_ref=dc_goal_intern_type(runtime,text,(uint16_t)(methods?methods:1));++dc_level_str_evidence.types;
    }
    if(!object_ref || runtime->error!=DC_GOAL_OK){free(block);return bad_link();}
    r+=n+1;
    do{
      uint32_t v=r[0],w;
      if(v&3u){v|=(uint32_t)r[1]<<8;
        if(v&2u){v|=(uint32_t)r[2]<<16;
          if(v&1u){v|=(uint32_t)r[3]<<24;r+=4;} else r+=3;} else r+=2;} else r+=1;
      obj+=v&~3u;
      if(obj+4>end){free(block);return bad_link();}
      memcpy(&w,obj,4);
      if(w!=UINT32_MAX){free(block);return bad_link();}
      memcpy(obj,&object_ref,4);
    }while(r<block+link_bytes && *r);
    ++r;
  }
  free(block);
  at=base+4u;
  ++dc_level_str_evidence.linked;dc_level_str_evidence.last_root=at;
  return at;
}

void dc_level_str_install(dc_goal_runtime *rt){
  runtime=rt;dc_goal_str_handler=str_rpc;dc_goal_link_handler=link_v2;
}
/* Each frame: both spool buffers' status (+8) and how many frames any was
 * 'locked or 'active (an art-group in use). --dump dc_level_spool_probe 64 */
volatile struct { uint32_t frames,locked_frames,active_frames,pad;char status[2][24]; } dc_level_spool_probe;
void dc_level_str_sample(dc_goal_runtime *rt){
  uint32_t control,i,locked=0,active=0;enum dc_goal_error prior;
  if(!rt || rt!=runtime)return;
  prior=rt->error;control=named("*art-control*");
  ++dc_level_spool_probe.frames;
  for(i=0;control && control!=rt->s7 && i<2;++i){
    unsigned char *p=dc_goal_resolve(rt,control+4u*i,4),*q;uint32_t b=0,st=0;const char *name;unsigned k;
    if(p)memcpy(&b,p,4);
    if(!b || b==rt->s7 || !(q=dc_goal_resolve(rt,b+8u,4)))continue;
    memcpy(&st,q,4);name=dc_goal_symbol_name(rt,st);
    for(k=0;k<23;++k)dc_level_spool_probe.status[i][k]=name && name[k]?name[k]:0;
    if(name && !strcmp(name,"locked"))locked=1;
    if(name && !strcmp(name,"active"))active=1;
  }
  dc_level_spool_probe.locked_frames+=locked;dc_level_spool_probe.active_frames+=active;
  rt->error=prior;
}
void dc_level_str_summary(dc_level_str_counts *out){
  out->served=dc_level_str_evidence.served;out->linked=dc_level_str_evidence.linked;
  out->link_errors=dc_level_str_evidence.link_errors;out->active_frames=dc_level_spool_probe.active_frames;
  out->files=dc_level_str_evidence.files;out->chunk_requests=dc_level_str_evidence.chunk_requests;
  out->chunk_served=dc_level_str_evidence.chunk_served;
}
