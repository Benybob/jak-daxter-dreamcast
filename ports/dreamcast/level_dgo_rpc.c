/* The IOP's DGO server and the C linker for original level loading (AI-assisted), ISC.
 * Original load-begin/load-continue (level.gc) drive *load-dgo-rpc* (RPC-DGO,
 * port 3) and then link each object with link-begin. On PS2 the IOP streams
 * objects into two buffers and the last one straight to the heap top it was
 * given. Here loading is synchronous: every object is read to the `bt` address
 * of the request (align64 of the level heap's current), and a v4 data object is
 * linked in place there as it is read. link-begin then does what the C linker
 * does after that: advance the heap past the object and, with execute-login,
 * call the object's original relocate method (bsp-header relocate attaches it
 * to the loading level). Code objects are not served yet: a level's code is
 * loaded at boot (build_level_code.py). --dump dc_level_dgo_evidence 88 */
#include "level_dgo_rpc.h"
#include "goal_art.h"
#include "goal_bridge.h"
#include <stdio.h>
#include <string.h>

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

static dc_goal_runtime *runtime;
static FILE *dgo;
static uint32_t dgo_count,dgo_next,dgo_at;

static uint32_t word(const unsigned char *p){
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void close_dgo(void){if(dgo)fclose(dgo);dgo=NULL;dgo_count=dgo_next=dgo_at=0;}
static int load_next(uint32_t bt){
  unsigned char header[64];unsigned char *at;uint32_t size,bytes=0;
  if(!dgo || dgo_next>=dgo_count || (bt&63u))return 0;
  if(fseek(dgo,(long)dgo_at,SEEK_SET) || fread(header,1,64,dgo)!=64)return 0;
  size=word(header);
  if(size<64 || (size&15u))return 0;
  if(!dc_goal_link_v4_at(runtime,dgo,dgo_at+64u,size,bt+64u,&bytes))return 0;
  if(!(at=dc_goal_resolve(runtime,bt,64)))return 0;
  /* The dgo-header GOAL reads: length (the linked data) and the root name. */
  memcpy(at+4,header+4,60);memcpy(at,&bytes,4);
  dc_level_dgo_evidence.last_bt=bt;dc_level_dgo_evidence.last_bytes=bytes;
  ++dc_level_dgo_evidence.loaded;
  ++dgo_next;dgo_at+=64u+size;
  return 1;
}
static uint64_t dgo_rpc(uint64_t port,uint64_t fno,uint64_t async,uint64_t send,
                        uint64_t send_bytes,uint64_t recv,uint64_t recv_bytes){
  unsigned char *msg,*reply;uint32_t bt,result=RESULT_ERROR,b1=0;
  (void)async;(void)send_bytes;
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
    if((dgo=fopen(path,"rb"))){
      unsigned char header[64];
      dc_level_dgo_evidence.opened=1;
      if(fread(header,1,64,dgo)==64 && (dgo_count=word(header))>0 && dgo_count<64){dgo_at=64;dc_level_dgo_evidence.count=dgo_count;}
      else close_dgo();
    }
  } else if(fno==LOAD_NEXT) ++dc_level_dgo_evidence.nexts;
  else if(fno==LOAD_CANCEL){close_dgo();result=RESULT_ABORTED;}
  if(fno!=LOAD_CANCEL){
    if(load_next(bt)){b1=bt;result=dgo_next==dgo_count?RESULT_DONE:RESULT_MORE;if(result==RESULT_DONE)close_dgo();}
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
}
