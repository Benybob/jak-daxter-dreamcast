/* The IOP's DGO server and the C linker for original level loading (AI-assisted), ISC.
 * Original load-begin/load-continue (level.gc) drive *load-dgo-rpc* (RPC-DGO,
 * port 3) and then link each object with link-begin. On PS2 the IOP streams
 * objects into two buffers and the last one straight to the heap top it was
 * given. Here loading is synchronous: every object is read to the `bt` address
 * of the request (align64 of the level heap's current), and a v4 data object is
 * linked in place there as it is read. link-begin then does what the C linker
 * does after that: advance the heap past the object and, with execute-login,
 * call the object's original relocate method (bsp-header relocate attaches it
 * to the loading level). A level's code objects are linked by the server
 * itself (load_code). --dump dc_level_dgo_evidence 88 */
#include "level_dgo_rpc.h"
#include "goal_art.h"
#include "goal_bridge.h"
#include "goal_native_file.h"
#include "goal_stack_call.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <kos.h>
#include <arch/cache.h>

enum { RPC_DGO=3, LOAD_BEGIN=0, LOAD_NEXT=1, LOAD_CANCEL=2 };
enum { RESULT_DONE=0, RESULT_ERROR=1, RESULT_MORE=2, RESULT_ABORTED=3 };
enum { LINK_EXECUTE_LOGIN=1u<<2 };

volatile struct {
  uint32_t version,begins,nexts,loaded,last_result,last_bt,last_bytes,errors;
  uint32_t links,relocated,relocate_result,error,site,rpc_other;
  char name[16];
  uint32_t prior_error,prior_site,opened,count;
} dc_level_dgo_evidence={.version=2};
typedef char dc_level_dgo_evidence_bytes[sizeof dc_level_dgo_evidence==88?1:-1];

void dc_level_draw_open(const char *directory) __attribute__((weak));
static dc_goal_runtime *runtime;
static FILE *dgo;
static uint32_t dgo_count,dgo_next,dgo_at;

static uint32_t word(const unsigned char *p){
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void close_dgo(void){if(dgo)fclose(dgo);dgo=NULL;dgo_count=dgo_next=dgo_at=0;}
/* A level's code (AI-assisted). As in a retail level DGO, its code objects come
 * before its data (make_level_dgo.py); here they are native DCSH modules. The
 * server links each one at the level heap's current (the kheap loading-level
 * names: base, top, current, top-base), runs its top-level startup as the
 * original linker does, and moves current past it, so the code lives in its
 * level's heap and is gone when the heap is reused. GOAL never sees these
 * objects: the reply carries the first data object after them, at the heap's
 * new current. At the first object of a DGO every code entry registered
 * inside the heap (its last level's) is forgotten.
 * --dump dc_level_code_evidence 32 */
volatile struct {uint32_t objects,bytes,initializers,forgotten,failed,error,site,last_current;} dc_level_code_evidence;
typedef char dc_level_code_evidence_bytes[sizeof dc_level_code_evidence==32?1:-1];
enum {CODE_STACK_BYTES=64*1024};
static uint32_t code_stack;  /* arena memory, taken once at install */
static void sync_code(uint32_t allocation,uint32_t bytes,void *context){
  (void)context;dcache_flush_range(allocation,bytes);icache_flush_range(allocation,bytes);
}
static void method_set_depth(int32_t delta){
  enum dc_goal_error prior=runtime->error;uint32_t symbol=dc_goal_intern(runtime,"*enable-method-set*");
  dc_goal_set_symbol(runtime,symbol,dc_goal_symbol_value(runtime,symbol)+(uint32_t)delta);
  runtime->error=prior;
}
/* How much of the level heap a level's DGO takes (code and data, before
 * login): most, the heap's size, and by how much an object did not fit.
 * first_data and code_end are offsets from the heap base, for the last DGO.
 * --dump dc_level_heap_need 32 */
volatile struct {uint32_t most,size,refused,first_data,code_end,base,current_at_begin,objects;} dc_level_heap_need;
static int code_fail(void){
  ++dc_level_code_evidence.failed;dc_level_code_evidence.error=runtime->error;
  dc_level_code_evidence.site=dc_goal_error_site;runtime->divert_at=runtime->divert_end=0;runtime->scratch_at=runtime->scratch_end=0;
  dc_goal_release_temporary(runtime);
  return 0;
}
/* Loads the code objects at the DGO's position; *bt becomes the place for the
 * data object after them. 0 on a fault. */
static int load_code(uint32_t *bt){
  uint32_t symbol=dc_goal_find(runtime,"loading-level"),heap=symbol?dc_goal_symbol_value(runtime,symbol):0;
  unsigned char *fields=heap && heap!=runtime->s7?dc_goal_resolve(runtime,heap,16):NULL;
  runtime->error=DC_GOAL_OK;
  if(!fields)return 1;  /* no level heap named: data only, as before */
  if(dgo_next==0){
    uint32_t base=word(fields),top_base=word(fields+12);
    dc_level_heap_need.first_data=0;dc_level_heap_need.objects=0;dc_level_heap_need.current_at_begin=word(fields+8)-base;
    dc_level_code_evidence.forgotten+=dc_goal_code_forget(runtime,base,top_base);
  }
  while(dgo_next<dgo_count){
    unsigned char header[64],magic[4];uint32_t size,current,top;dc_native_file_result object;uint64_t value;
    if(fseek(dgo,(long)dgo_at,SEEK_SET) || fread(header,1,64,dgo)!=64 || fread(magic,1,4,dgo)!=4)return code_fail();
    if(memcmp(magic,"DCSH",4))break;
    size=word(header);
    current=(word(fields+8)+15u)&~15u;top=word(fields+4)&~15u;
    if(current>top || fseek(dgo,(long)dgo_at+64,SEEK_SET))return code_fail();
    runtime->divert_at=current;runtime->divert_end=top;
    /* the loader's temporary buffers from the heap's free top: the module's
     * image, tables and a 128 KB symbol snapshot, which the C heap no longer
     * has room for once a level's renderer holds its blocks */
    {uint32_t need=((size+31u)&~31u)*2u+DC_GOAL_SYMBOL_BYTES+65536u;
     if(top-current>need+size){runtime->scratch_at=(top-need)&~31u;runtime->scratch_end=top;runtime->divert_end=runtime->scratch_at;}}
    if(!dc_goal_load_native_stream_temporary(runtime,dgo,size,sync_code,NULL,&object))return code_fail();
    current=runtime->divert_at;runtime->divert_at=runtime->divert_end=0;runtime->scratch_at=runtime->scratch_end=0;
    memcpy(fields+8,&current,4);
    dc_level_code_evidence.bytes+=object.module.code_bytes;dc_level_code_evidence.last_current=current;
    dc_level_heap_need.code_end=current-word(fields);
    /* the startup's own allocations go where it asks; its types stay global */
    method_set_depth(1);
    /* On a stack of its own: this runs inside the display process's thread,
     * whose 14 KiB stack a file's top level (deftypes, static data, the
     * bridges under them) overruns into whatever the arena holds below it. */
    if(object.initializer_kind!=DC_NATIVE_NO_INITIALIZER){
      uint32_t symbol=dc_goal_find(runtime,object.initializer),entry=symbol?dc_goal_symbol_value(runtime,symbol):0;
      if(!entry || !code_stack || !dc_goal_call_on_stack(runtime,entry,code_stack,CODE_STACK_BYTES,&value)){
        method_set_depth(-1);return code_fail();}
      ++dc_level_code_evidence.initializers;
    }
    method_set_depth(-1);
    dc_goal_release_temporary(runtime);
    if(runtime->error!=DC_GOAL_OK)return code_fail();
    ++dc_level_code_evidence.objects;
    ++dgo_next;dgo_at+=64u+((size+15u)&~15u);
    /* GOAL computed bt before these objects took the heap */
    *bt=(word(fields+8)+63u)&~63u;
  }
  return 1;
}
static int load_next(uint32_t bt){
  unsigned char header[64];unsigned char *at;uint32_t size,bytes=0;
  if(!dgo || dgo_next>=dgo_count || (bt&63u))return 0;
  if(fseek(dgo,(long)dgo_at,SEEK_SET) || fread(header,1,64,dgo)!=64)return 0;
  size=word(header);
  if(size<64 || (size&15u))return 0;
  /* The object must fit below the level heap's top: linked in place, it is
   * not allocated, and past the top lies whatever the arena holds next. */
  {uint32_t symbol=dc_goal_find(runtime,"loading-level"),heap=symbol?dc_goal_symbol_value(runtime,symbol):0;
   unsigned char *fields=heap && heap!=runtime->s7?dc_goal_resolve(runtime,heap,16):NULL;
   runtime->error=DC_GOAL_OK;
   if(fields){
     uint32_t base=word(fields),top=word(fields+4),end=bt+64u+size;
     if(end-base>dc_level_heap_need.most)dc_level_heap_need.most=end-base;
     if(dgo_next<=dc_level_code_evidence.objects || !dc_level_heap_need.first_data || dc_level_heap_need.base!=base){dc_level_heap_need.base=base;}
     ++dc_level_heap_need.objects;if(!dc_level_heap_need.first_data)dc_level_heap_need.first_data=bt-base;
     dc_level_heap_need.size=top-base;
     if(end>top || end<bt){dc_level_heap_need.refused=end-top;runtime->error=DC_GOAL_OUT_OF_MEMORY;return 0;}
   }}
  if(!dc_goal_link_v4_at(runtime,dgo,dgo_at+64u,size,bt+64u,&bytes))return 0;
  if(!(at=dc_goal_resolve(runtime,bt,64)))return 0;
  /* The dgo-header GOAL reads: length (the linked data) and the root name. */
  memcpy(at+4,header+4,60);memcpy(at,&bytes,4);
  dc_level_dgo_evidence.last_bt=bt;dc_level_dgo_evidence.last_bytes=bytes;
  ++dc_level_dgo_evidence.loaded;
  ++dgo_next;dgo_at+=64u+size;
  return 1;
}
/* The IOP ramdisk (RPC-RAMDISK, port 2): fill (fno 1) names a file, here a
 * VIS file, and gives it an ee-id; load (fno 0) copies length bytes at offset of
 * that file into the receive buffer. The file stays on /cd and is read on
 * demand instead of being held in IOP memory. --dump dc_level_ramdisk_evidence 48 */
volatile struct { uint32_t fills,loads,failed,id,bytes,last_offset,last_length; char name[20]; } dc_level_ramdisk_evidence;
typedef char dc_level_ramdisk_evidence_bytes[sizeof dc_level_ramdisk_evidence==48?1:-1];
static FILE *ramdisk;static uint32_t ramdisk_id;
/* The file whole, as the IOP held it, when it is at most RAMDISK_RESIDENT and
 * the KOS heap has room (Geyser Rock's TRA.VIS is 215 KB): a load is then a
 * copy. Read from the disc on demand, each load was a disc command on the
 * game's thread, which also waits out any read another thread has in hand. */
enum {RAMDISK_RESIDENT=256*1024};
static unsigned char *ramdisk_image;static uint32_t ramdisk_bytes;
static void ramdisk_rpc(uint64_t fno,uint64_t send,uint64_t recv,uint64_t recv_bytes){
  unsigned char *msg=send<=UINT32_MAX?dc_goal_resolve(runtime,(uint32_t)send,32):NULL,*out;
  if(!msg){++dc_level_ramdisk_evidence.failed;return;}
  if(fno==1){
    char path[32]="/cd/";unsigned i,n=4;
    ++dc_level_ramdisk_evidence.fills;
    if(ramdisk)fclose(ramdisk);
    for(i=0;i<16 && msg[16+i] && n<sizeof path-1;++i){
      char c=(char)msg[16+i];path[n++]=(char)(c>='a' && c<='z'?c-32:c);
      if(i<19)dc_level_ramdisk_evidence.name[i]=path[n-1];
    }
    path[n]=0;memcpy(&ramdisk_id,msg+4,4);dc_level_ramdisk_evidence.id=ramdisk_id;
    free(ramdisk_image);ramdisk_image=NULL;ramdisk_bytes=0;
    if(!(ramdisk=fopen(path,"rb")))++dc_level_ramdisk_evidence.failed;
    else {long size=-1;
      /* 32-byte aligned, through the KOS descriptor: one DMA command */
      if(!fseek(ramdisk,0,SEEK_END) && (size=ftell(ramdisk))>0 && size<=RAMDISK_RESIDENT && !fseek(ramdisk,0,SEEK_SET) &&
         (ramdisk_image=memalign(32,((size_t)size+2047u)&~(size_t)2047u))){
        file_t fd=fs_open(path,O_RDONLY);size_t done=0;
        while(fd>=0 && done<(size_t)size){ssize_t got=fs_read(fd,ramdisk_image+done,(size_t)size-done);if(got<=0)break;done+=(size_t)got;}
        if(fd>=0)fs_close(fd);
        if(done==(size_t)size)ramdisk_bytes=(uint32_t)size;
        else {free(ramdisk_image);ramdisk_image=NULL;}
      }
    }
  } else if(fno==0){
    uint32_t id,offset,length;
    memcpy(&id,msg+4,4);memcpy(&offset,msg+8,4);memcpy(&length,msg+12,4);
    ++dc_level_ramdisk_evidence.loads;
    dc_level_ramdisk_evidence.last_offset=offset;dc_level_ramdisk_evidence.last_length=length;
    if(!ramdisk || id!=ramdisk_id || recv>UINT32_MAX || length>recv_bytes ||
       !(out=dc_goal_resolve(runtime,(uint32_t)recv,length)) ||
       (ramdisk_image?(offset>ramdisk_bytes || length>ramdisk_bytes-offset || !memcpy(out,ramdisk_image+offset,length)):
        (fseek(ramdisk,(long)offset,SEEK_SET) || fread(out,1,length,ramdisk)!=length))){
      ++dc_level_ramdisk_evidence.failed;runtime->error=DC_GOAL_OK;return;
    }
    dc_level_ramdisk_evidence.bytes+=length;
  }
}
static uint64_t dgo_rpc(uint64_t port,uint64_t fno,uint64_t async,uint64_t send,
                        uint64_t send_bytes,uint64_t recv,uint64_t recv_bytes){
  unsigned char *msg,*reply;uint32_t bt,result=RESULT_ERROR,b1=0;
  (void)async;(void)send_bytes;
  if(port==2 && runtime){ramdisk_rpc(fno,send,recv,recv_bytes);return 0;}
  if(port!=RPC_DGO || !runtime){++dc_level_dgo_evidence.rpc_other;return 0;}
  if(send>UINT32_MAX || recv>UINT32_MAX || recv_bytes<16 ||
     !(msg=dc_goal_resolve(runtime,(uint32_t)send,32)) || !(reply=dc_goal_resolve(runtime,(uint32_t)recv,16)))
    return 0;
  bt=word(msg+12);
  /* The IOP never sees an EE-side fault: a pending runtime error (a refused
   * kmalloc of the two 2 MiB load buffers, say) is recorded, not inherited. */
  if(runtime->error!=DC_GOAL_OK && !dc_level_dgo_evidence.prior_error){
    dc_level_dgo_evidence.prior_error=runtime->error;dc_level_dgo_evidence.prior_site=dc_goal_error_site;}
  runtime->error=DC_GOAL_OK;
  if(fno==LOAD_BEGIN){
    char path[32]="/cd/";unsigned i,n=4;
    ++dc_level_dgo_evidence.begins;close_dgo();
    for(i=0;i<16 && msg[16+i] && n<sizeof path-1;++i){
      char c=(char)msg[16+i];path[n++]=(char)(c>='a' && c<='z'?c-32:c);
      dc_level_dgo_evidence.name[i]=path[n-1];
    }
    path[n]=0;
    {/* the renderer follows the level being loaded: its render folder on the disc */
     static const char *const folders[][2]={{"TRAINING","GEYSER"}};
     char stem[20];const char *folder=stem;unsigned k;
     for(k=0;k<sizeof stem-1 && path[4+k] && path[4+k]!='.';++k)stem[k]=path[4+k];
     stem[k]=0;
     for(k=0;k<sizeof folders/sizeof folders[0];++k)if(!strcmp(stem,folders[k][0]))folder=folders[k][1];
     if(dc_level_draw_open)dc_level_draw_open(folder);}
    if((dgo=fopen(path,"rb"))){
      unsigned char header[64];
      dc_level_dgo_evidence.opened=1;
      if(fread(header,1,64,dgo)==64 && (dgo_count=word(header))>0 && dgo_count<64){dgo_at=64;dc_level_dgo_evidence.count=dgo_count;}
      else close_dgo();
    }
  } else if(fno==LOAD_NEXT) ++dc_level_dgo_evidence.nexts;
  else if(fno==LOAD_CANCEL){close_dgo();result=RESULT_ABORTED;}
  if(fno!=LOAD_CANCEL){
    if(dgo && load_code(&bt) && load_next(bt)){b1=bt;result=dgo_next==dgo_count?RESULT_DONE:RESULT_MORE;if(result==RESULT_DONE)close_dgo();}
    else {++dc_level_dgo_evidence.errors;dc_level_dgo_evidence.error=runtime->error;
          dc_level_dgo_evidence.site=dc_goal_error_site;runtime->error=DC_GOAL_OK;close_dgo();}
  }
  
  dc_level_dgo_evidence.last_result=result;
  reply[2]=(unsigned char)result;reply[3]=(unsigned char)(result>>8);
  memcpy(reply+4,&b1,4);
  return 0;
}
static uint64_t dgo_link_begin(uint64_t data,uint64_t name,uint64_t length,uint64_t heap,uint64_t flags){
  typedef uint64_t (*relocate_fn)(uint64_t,uint64_t,uint64_t);
  unsigned char *fields;uint32_t current,root,type,method;
  if(!runtime || data>UINT32_MAX || length>UINT32_MAX || heap>UINT32_MAX ||
     !(fields=dc_goal_resolve(runtime,(uint32_t)heap,16)))return 0;
  ++dc_level_dgo_evidence.links;
  current=((uint32_t)data+(uint32_t)length+15u)&~15u;
  memcpy(fields+8,&current,4);  /* kheap current */
  if(flags&LINK_EXECUTE_LOGIN){
    root=(uint32_t)data+4u;type=dc_goal_type_of(runtime,root);
    method=type?dc_goal_get_method(runtime,type,7):0;
    if(!method){dc_level_dgo_evidence.error=runtime->error;return 0;}
    dc_level_dgo_evidence.relocate_result=(uint32_t)((relocate_fn)(uintptr_t)method)(root,heap,name);
    ++dc_level_dgo_evidence.relocated;
  }
  return 1;
}
void dc_level_dgo_install(dc_goal_runtime *rt){
  runtime=rt;dc_goal_rpc_handler=dgo_rpc;dc_goal_link_begin_handler=dgo_link_begin;
  if(!code_stack){enum dc_goal_error prior=rt->error;code_stack=dc_goal_alloc(rt,CODE_STACK_BYTES);rt->error=prior;}
}
