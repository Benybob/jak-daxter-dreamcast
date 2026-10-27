/* Original bounded process bump allocation (AI-assisted), ISC. */
#include "goal_process_heap.h"
#include <limits.h>
#include <string.h>
enum { PROCESS_BYTES=112, DYNAMIC_OFFSET=108, LENGTH_OFFSET=68,
       BASE_OFFSET=76, TOP_OFFSET=80, CUR_OFFSET=84 };
static uint32_t fail(dc_goal_runtime *rt,enum dc_goal_error error) {
  if(rt) rt->error=error;
  return 0;
}
static uint32_t read_word(const unsigned char *p,unsigned offset) {
  uint32_t result; memcpy(&result,p+offset,4); return result;
}
static unsigned char *validate(const dc_goal_process_heap *heap,uint32_t *cur,uint32_t *top) {
  dc_goal_runtime *rt;
  unsigned char *fields;
  uint32_t process_type,base,end;
  dc_goal_type_fields *schema;
  if(!heap || !(rt=heap->runtime)) return NULL;
  if(heap->process<4 || heap->allocation_bytes<PROCESS_BYTES ||
     heap->allocation_bytes>UINT32_MAX-(heap->process-4)) {
    (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT)); return NULL;
  }
  process_type=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE);
  if(!process_type || dc_goal_type_of(rt,process_type)!=rt->type_type ||
     dc_goal_type_of(rt,heap->process)!=process_type) {
    fail(rt,DC_GOAL_BAD_TYPE); return NULL;
  }
  schema=dc_goal_resolve(rt,process_type,sizeof(*schema));
  if(!schema || schema->allocated_size!=PROCESS_BYTES) {
    fail(rt,DC_GOAL_BAD_TYPE); return NULL;
  }
  if(!dc_goal_resolve(rt,heap->process-4,heap->allocation_bytes)) return NULL;
  fields=dc_goal_resolve(rt,heap->process,PROCESS_BYTES-4);
  if(!fields) return NULL;
  base=heap->process+DYNAMIC_OFFSET; end=heap->process-4+heap->allocation_bytes;
  *cur=read_word(fields,CUR_OFFSET); *top=read_word(fields,TOP_OFFSET);
  if(read_word(fields,LENGTH_OFFSET)!=heap->allocation_bytes-PROCESS_BYTES ||
     read_word(fields,BASE_OFFSET)!=base || (base&15u) ||
     *cur<base || *cur>*top || *top>end || (*cur&15u)) {
    (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT)); return NULL;
  }
  return fields;
}
int dc_goal_process_heap_open(dc_goal_process_heap *heap,dc_goal_runtime *rt,
                               uint32_t process,uint32_t allocation_bytes) {
  dc_goal_process_heap candidate; uint32_t cur,top;
  if(!heap) { (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT)); return 0; }
  memset(heap,0,sizeof(*heap));
  if(!rt || !rt->memory) { (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT)); return 0; }
  candidate.runtime=rt; candidate.process=process; candidate.allocation_bytes=allocation_bytes;
  if(!validate(&candidate,&cur,&top)) return 0;
  *heap=candidate; rt->error=DC_GOAL_OK; return 1;
}
uint32_t dc_goal_process_alloc(dc_goal_process_heap *heap,uint32_t bytes) {
  uint32_t cur,top,aligned,next;
  unsigned char *fields,*destination;
  dc_goal_runtime *rt=heap?heap->runtime:NULL;
  if(!rt || !bytes || bytes>(uint32_t)INT32_MAX-15u) return (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT));
  fields=validate(heap,&cur,&top);
  if(!fields) return 0;
  aligned=(bytes+15u)&~15u;
  /* Preserve original strict comparison, including exact-fit rejection. */
  if(aligned>=top-cur) return fail(rt,DC_GOAL_OUT_OF_MEMORY);
  destination=dc_goal_resolve(rt,cur,aligned);
  if(!destination) return 0;
  next=cur+aligned;
  memset(destination,0,aligned);
  memcpy(fields+CUR_OFFSET,&next,4);
  rt->error=DC_GOAL_OK; return cur;
}
uint32_t dc_goal_process_new_basic(dc_goal_process_heap *heap,uint32_t type,uint32_t bytes) {
  dc_goal_runtime *rt=heap?heap->runtime:NULL;
  dc_goal_type_fields *fields;
  uint32_t allocation;
  if(!rt) return 0;
  if(!dc_goal_is_subtype(rt,type,rt->basic_type)) return (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT));
  fields=dc_goal_resolve(rt,type,sizeof(*fields));
  if(!fields) return 0;
  if(bytes<4 || bytes<fields->allocated_size) return (DC_GOAL_NOTE_SITE(3),fail(rt,DC_GOAL_BAD_ARGUMENT));
  allocation=dc_goal_process_alloc(heap,bytes);
  if(!allocation) return 0;
  memcpy(dc_goal_resolve(rt,allocation,4),&type,4);
  return allocation+4;
}
