/* Independent widened C contract for low-32 division (AI-assisted), ISC. */
#ifndef DC_DIVISION_VECTORS_H
#define DC_DIVISION_VECTORS_H
#include <stdint.h>
static const uint64_t dc_div_values[]={0,1,2,3,7,31,127,65535,UINT64_C(0x7fffffff),
  UINT64_C(0x80000000),UINT64_C(0x80000001),UINT64_C(0xffffffff),
  UINT64_C(0x100000001),UINT64_C(0xffffffff00000005),UINT64_C(0x0123456789abcdef)};
#define DC_DIV_VALUE_COUNT (sizeof(dc_div_values)/sizeof(dc_div_values[0]))
static uint64_t dc_div_sign_extend(uint32_t bits) {
  return bits&UINT32_C(0x80000000)?(uint64_t)((int64_t)bits-INT64_C(0x100000000)):(uint64_t)bits;
}
static int dc_div_valid(unsigned kind,uint64_t a,uint64_t b) {
  uint32_t low_a=(uint32_t)a,low_b=(uint32_t)b;
  return low_b && (kind>=2 || low_a!=UINT32_C(0x80000000) || low_b!=UINT32_MAX);
}
static uint64_t dc_div_expected(unsigned kind,uint64_t a,uint64_t b) {
  uint32_t x=(uint32_t)a,y=(uint32_t)b,result;
  if (kind<2) {
    int64_t sx=x&UINT32_C(0x80000000)?(int64_t)x-INT64_C(0x100000000):(int64_t)x;
    int64_t sy=y&UINT32_C(0x80000000)?(int64_t)y-INT64_C(0x100000000):(int64_t)y;
    result=(uint32_t)(kind==1?sx%sy:sx/sy);
  } else result=kind==3?x%y:x/y;
  return dc_div_sign_extend(result);
}
#endif
