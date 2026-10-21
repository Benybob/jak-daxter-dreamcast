/* Independent unsigned C expectations for x86 count semantics (AI-assisted), ISC. */
#ifndef DC_SHIFT_VECTORS_H
#define DC_SHIFT_VECTORS_H
#include <stdint.h>
static const uint64_t dc_shift_values[]={0,1,UINT64_MAX,UINT64_C(0x8000000000000000),
  UINT64_C(0x7fffffffffffffff),UINT64_C(0x0123456789abcdef),UINT64_C(0xfedcba9876543210)};
static const uint64_t dc_shift_extra_counts[]={64,65,95,96,127,128,UINT64_MAX,
  (uint64_t)-31,(uint64_t)-32,(uint64_t)-33,(uint64_t)-63,(uint64_t)-64,(uint64_t)-65,
  UINT64_C(0x100000001),UINT64_C(0x8000000000000000)};
static uint64_t dc_shift_expected(unsigned kind,uint64_t value,uint64_t amount) {
  unsigned n=(unsigned)(amount&63u);
  if (!n) return value;
  if (kind==0) return value<<n;
  if (kind==1 || !(value>>63)) return value>>n;
  return (value>>n)|(UINT64_MAX<<(64u-n));
}
static uint64_t dc_shift_count(unsigned index) {
  return index<64?index:dc_shift_extra_counts[index-64];
}
#define DC_SHIFT_VALUE_COUNT (sizeof(dc_shift_values)/sizeof(dc_shift_values[0]))
#define DC_SHIFT_COUNT_COUNT (64u+sizeof(dc_shift_extra_counts)/sizeof(dc_shift_extra_counts[0]))
#endif
