/* Exact shim-push boundaries, not an arbitrary stack-budget proof.
 * Root SP is normalized to the runtime's29-bit address domain by its caller.
 * (AI-assisted), ISC. */
#ifndef DC_FIBER_BOUNDS_H
#define DC_FIBER_BOUNDS_H
#include <stdint.h>
static inline int dc_fiber_leave_push_ok(uint32_t base,uint32_t bytes,uint32_t sp){
 if(bytes<4 || bytes>UINT32_MAX-base || (sp&3u))return 0;
 return sp>=base+4u && sp<=base+bytes;
}
static inline int dc_fiber_root_push_ok(uint32_t base,uint32_t bytes,uint32_t sp){
 uint32_t top;
 if(!bytes || bytes>UINT32_MAX-base || sp<4u || (sp&3u))return 0;
 top=base+bytes;
 /* Keep both the live-SP marker and the shim's [SP-4,SP) outside working span. */
 return sp<base || sp-4u>=top;
}
#endif
