/* Real portable allocator guards, also compiled for SH-4 (AI-assisted), ISC. */
#include "process_heap_cases.h"
#include "goal_process_heap.h"
#include "goal_kernel_types.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static uint32_t word(dc_goal_runtime *rt,uint32_t object,unsigned offset) {
  uint32_t v; memcpy(&v,dc_goal_resolve(rt,object+offset,4),4); return v;
}
static void set_word(dc_goal_runtime *rt,uint32_t object,unsigned offset,uint32_t value) {
  memcpy(dc_goal_resolve(rt,object+offset,4),&value,4);
}
static uint32_t owner(dc_goal_runtime *rt,uint32_t type,uint32_t bytes) {
  uint32_t p=dc_goal_new_basic(rt,type,112+bytes);
  if(!p) return 0;
  set_word(rt,p,68,bytes); set_word(rt,p,76,p+108);
  set_word(rt,p,80,p+108+bytes); set_word(rt,p,84,p+108);
  return p;
}
void dc_process_heap_cases(dc_runtime_check check,void *context,unsigned *id) {
  const uint32_t capacity=524288;
  void *allocation=malloc(capacity+64);
  unsigned char *memory;
  dc_goal_runtime rt;
  dc_kernel_types_result installed;
  dc_goal_process_heap heap,valid;
  uint32_t process_type,p,base,used,cur,result,record,type,small,odd,empty,guard;
  const uint32_t requests[]={1,15,16};
  const unsigned offsets[]={68,76,80,84,84,80,84,68};
  uint32_t corrupt[8],values[8],old;
  unsigned char snapshot[192],zero[32]={0},sentinel[16];
  unsigned i;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(allocation!=NULL,1,"process heap guard fixture allocation");
  if(!allocation) return;
  memory=(unsigned char*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u)+16;
  memset(memory-16,0xa5,capacity+32);
  CHECK(dc_goal_init(&rt,memory,capacity,0x01000000u),1,"portable heap runtime initialization");
  CHECK(dc_goal_install_kernel_types(&rt,&installed),1,"portable original process schema installation");
  if(!installed.types) { free(allocation); return; }
  process_type=dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_PROCESS_TYPE);
  p=owner(&rt,process_type,64); small=owner(&rt,process_type,16);
  odd=owner(&rt,process_type,17); empty=owner(&rt,process_type,0);
  guard=dc_goal_alloc(&rt,16);
  CHECK(p && small && odd && empty && guard,1,"portable process reservation fixtures");
  if(!p || !small || !odd || !empty || !guard) { free(allocation); return; }
  memset(dc_goal_resolve(&rt,guard,16),0x5a,16); memset(sentinel,0xa5,16);
  base=p+108; memset(dc_goal_resolve(&rt,base,64),0xa5,64); used=rt.used;
  CHECK(dc_goal_process_heap_open(&heap,&rt,p,176),1,"explicit owned reservation opens");
  valid=heap;
  for(i=0;i<3;++i) {
    CHECK(dc_goal_process_alloc(&heap,requests[i]),base+i*16,"round16 process allocation address");
    CHECK(word(&rt,p,84),base+(i+1)*16,"original heap-cur advances by aligned size");
    CHECK(memcmp(dc_goal_resolve(&rt,base+i*16,16),zero,16),0,"process aligned allocation is cleared");
    CHECK(rt.used,used,"process allocation never consumes global arena");
  }
  CHECK(dc_goal_process_alloc(&heap,16),0,"original exact-fit process request rejects");
  CHECK(rt.error,DC_GOAL_OUT_OF_MEMORY,"exact fit returns explicit OOM");
  CHECK(word(&rt,p,84),base+48,"exact-fit failure preserves cursor");
  CHECK(memcmp(dc_goal_resolve(&rt,base+48,16),sentinel,16),0,"exact-fit failure preserves destination bytes");
  CHECK(rt.used,used,"exact fit cannot fall back to global allocation");
  CHECK(dc_goal_process_heap_open(&heap,&rt,small,128),1,"sixteen-byte reservation opens");
  CHECK(dc_goal_process_alloc(&heap,1),0,"round16 exact fit rejects even one-byte request");
  CHECK(word(&rt,small,84),small+108,"small exact fit preserves cursor");
  CHECK(dc_goal_process_heap_open(&heap,&rt,odd,129),1,"unaligned exclusive heap top is allowed");
  CHECK(dc_goal_process_alloc(&heap,16),odd+108,"sixteen bytes fit with one byte remaining");
  CHECK(word(&rt,odd,84),odd+124,"odd reservation cursor aligned");
  CHECK(dc_goal_process_alloc(&heap,1),0,"last single byte cannot fit aligned allocation");
  CHECK(dc_goal_process_heap_open(&heap,&rt,empty,112),1,"zero-capacity process heap opens");
  CHECK(dc_goal_process_alloc(&heap,1),0,"zero-capacity heap cannot allocate");
  CHECK(word(&rt,empty,84),empty+108,"empty heap cursor unchanged");
  set_word(&rt,p,84,base);
  corrupt[0]=80; corrupt[1]=base+16; corrupt[2]=base+80; corrupt[3]=base+1;
  corrupt[4]=base-16; corrupt[5]=base-16; corrupt[6]=base+80; corrupt[7]=UINT32_MAX;
  for(i=0;i<8;++i) {
    heap=valid; old=word(&rt,p,offsets[i]); set_word(&rt,p,offsets[i],corrupt[i]);
    memcpy(snapshot,dc_goal_resolve(&rt,p-4,176),176);
    CHECK(dc_goal_process_alloc(&heap,1),0,"mutated process metadata rejected on every allocation");
    CHECK(rt.error,DC_GOAL_BAD_ARGUMENT,"mutated heap metadata has explicit error");
    CHECK(memcmp(snapshot,dc_goal_resolve(&rt,p-4,176),176),0,"invalid metadata changes no cursor or data bytes");
    CHECK(rt.used,used,"invalid metadata creates no global allocation");
    set_word(&rt,p,offsets[i],old);
  }
  memcpy(snapshot,dc_goal_resolve(&rt,p-4,176),176);
  for(i=0;i<3;++i) {
    const uint32_t invalid[]={0,UINT32_MAX,INT32_MAX};
    heap=valid;
    CHECK(dc_goal_process_alloc(&heap,invalid[i]),0,"invalid or overflowing process size rejects");
    CHECK(rt.error,DC_GOAL_BAD_ARGUMENT,"invalid process size has explicit error");
    CHECK(memcmp(snapshot,dc_goal_resolve(&rt,p-4,176),176),0,"invalid size preserves all reservation bytes");
  }
  heap=valid;
  CHECK(dc_goal_process_heap_open(&heap,&rt,p,192),0,"wrong caller-owned reservation size rejected");
  CHECK(heap.runtime==NULL,1,"failed open clears former heap view");
  type=word(&rt,p-4,0); set_word(&rt,p-4,0,rt.basic_type);
  heap=valid;
  CHECK(dc_goal_process_alloc(&heap,1),0,"owner type is revalidated");
  CHECK(rt.error,DC_GOAL_BAD_TYPE,"wrong owner type explicit error");
  set_word(&rt,p-4,0,type);
  { dc_goal_type_fields *schema=dc_goal_resolve(&rt,process_type,sizeof(*schema));
    uint16_t size=schema->allocated_size;
    schema->allocated_size=8; heap=valid;
    CHECK(dc_goal_process_alloc(&heap,1),0,"original process schema size revalidated");
    CHECK(rt.error,DC_GOAL_BAD_TYPE,"incompatible process schema rejects");
    CHECK(memcmp(snapshot,dc_goal_resolve(&rt,p-4,176),176),0,"incompatible schema preserves reservation");
    schema->allocated_size=size;
  }
  CHECK(dc_goal_process_heap_open(&heap,&rt,p,176),1,"valid view reopens after guards");
  record=dc_goal_define_type(&rt,"dc-portable-process-record",rt.basic_type,16,0,9);
  CHECK(record!=0,1,"portable sized basic schema");
  used=rt.used;
  result=dc_goal_process_new_basic(&heap,record,16);
  CHECK(result,base+4,"process basic uses original four-byte tag offset");
  CHECK(dc_goal_type_of(&rt,result),record,"process basic type header linked");
  CHECK(memcmp(dc_goal_resolve(&rt,result,12),zero,12),0,"process basic body is cleared");
  CHECK(word(&rt,p,84),base+16,"process basic advances original cursor");
  CHECK(rt.used,used,"process basic allocates within reservation only");
  CHECK(dc_goal_process_new_basic(&heap,record,17),base+20,"sized process basic rounds requested size");
  CHECK(word(&rt,p,84),base+48,"sized process basic cursor");
  CHECK(dc_goal_process_new_basic(&heap,record,16),0,"typed process exact fit rejects");
  CHECK(word(&rt,p,84),base+48,"typed exact fit cursor unchanged");
  memcpy(snapshot,dc_goal_resolve(&rt,p-4,176),176);
  CHECK(dc_goal_process_new_basic(&heap,record,8),0,"type minimum allocation size enforced");
  CHECK(dc_goal_process_new_basic(&heap,rt.object_type,16),0,"non-basic type rejected");
  CHECK(memcmp(snapshot,dc_goal_resolve(&rt,p-4,176),176),0,"typed allocation guards preserve full reservation");
  CHECK(dc_goal_process_alloc(NULL,16),0,"null view rejected");
  CHECK(dc_goal_process_heap_open(NULL,&rt,p,176),0,"null output view rejected");
  CHECK(dc_goal_process_heap_open(&heap,NULL,p,176),0,"null runtime rejected");
  CHECK(heap.runtime==NULL,1,"null-runtime open clears view");
  memset(values,0x5a,16);
  CHECK(memcmp(dc_goal_resolve(&rt,guard,16),values,16),0,"neighbor allocation guard unchanged");
  CHECK(memcmp(memory-16,sentinel,16),0,"arena leading guard unchanged");
  CHECK(memcmp(memory+capacity,sentinel,16),0,"arena trailing guard unchanged");
  /* Keep otherwise-unused locals out of the public allocator interface. */
  cur=word(&rt,p,84); CHECK(cur,base+48,"all failure guards preserve final process cursor");
  free(allocation);
#undef CHECK
}
