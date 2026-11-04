/* Private fixture split; old ELFs never include this header (AI-assisted), ISC. */
#ifndef DC_ARENA_JAK_MEMORY_H
#define DC_ARENA_JAK_MEMORY_H
#include "build/arena-jak-budget/budget.h"
#ifndef DC_JAK_RENDER_SLAB_BYTES
#define DC_JAK_RENDER_SLAB_BYTES DC_JAK_SLAB_BYTES
#endif
#if DC_JAK_RENDER_SLAB_BYTES < DC_JAK_RENDER_MEASURED + DC_JAK_RENDER_MARGIN
#error Jak renderer reserve is below measured fixture need plus margin
#endif
#if DC_JAK_RENDER_SLAB_BYTES > 2621440u || (DC_JAK_RENDER_SLAB_BYTES & 31u)
#error Jak renderer reserve must be 32-byte aligned and at most the old slab
#endif
#define DC_JAK_ARENA_BYTES (13631488u-DC_JAK_RENDER_SLAB_BYTES)
#include <stdint.h>
typedef struct {uint32_t borrowed,load_capacity,boot_offset;} dc_jak_memory_layout;
static inline int dc_jak_memory_plan(uint32_t boot_bytes,dc_jak_memory_layout *p){
 uint32_t rounded;
 if(!p || !boot_bytes || boot_bytes>2u*1024u*1024u)return 0;
 rounded=(boot_bytes+31u)&~31u;
 p->borrowed=rounded>DC_JAK_RENDER_SLAB_BYTES?rounded-DC_JAK_RENDER_SLAB_BYTES:0;
 p->load_capacity=DC_JAK_ARENA_BYTES-p->borrowed;
 p->boot_offset=DC_JAK_ARENA_BYTES+32u-p->borrowed;
 return p->load_capacity>=11010048u;
}
#endif
