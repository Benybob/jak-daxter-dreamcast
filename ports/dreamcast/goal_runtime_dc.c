/* Native GOAL address domain (AI-assisted), ISC. */
#include <kos.h>
#include <arch/memory.h>
#include <arch/mmu.h>
#include "goal_runtime.h"

int dc_goal_init_native(dc_goal_runtime *rt,void *memory,uint32_t capacity) {
  uint32_t base=(uint32_t)(uintptr_t)memory&MEM_AREA_CACHE_MASK;
  /* Keep all native GOAL references in cached P0 below bit 31. Original
   * signed object-cell loads and unsigned constants then agree on pointers,
   * while negative bintegers retain their original sign extension.
   * KOS P1 pointers are used only behind the C resolver/bridge boundary.
   * P0 is a direct hardware alias only while address translation is off. */
  if (!rt) return 0;
  if (mmu_enabled() || base<0x0c000000u || base>0x0d000000u ||
      capacity>0x0d000000u-base) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  return dc_goal_init(rt,memory,capacity,base);
}
