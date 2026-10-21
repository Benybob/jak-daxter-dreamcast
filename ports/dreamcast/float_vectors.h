/* Pinned frontend/SSE predicate contract (AI-assisted), ISC. */
#ifndef DC_FLOAT_VECTORS_H
#define DC_FLOAT_VECTORS_H
#include <stdint.h>
typedef struct { uint32_t left,right,mask; } dc_float_vector;
/* Bits: EQ, NE, LT, LE, GT, GE. Unordered follows UCOMISS conditions. */
static const dc_float_vector dc_float_vectors[]={
  {0xc0000000u,0x3f800000u,14},{0x40400000u,0x3f800000u,50},
  {0x3f800000u,0x3f800000u,41},{0,0x80000000u,41},
  {0x7f800000u,0x3f800000u,50},{0xff800000u,0x3f800000u,14},
  {0x7fc01234u,0x3f800000u,13},{0x3f800000u,0x7fc01234u,13},
  {0x7fc01234u,0x7fc01234u,13}
};
static const uint32_t dc_float_int_inputs[]={0x4079999au,0xc079999au,0,0x80000000u,
  0x4effffffu,0x4f000000u,0xcf000000u,0xcf000001u,0x7fc01234u,0x7f800000u,0xff800000u};
static const int32_t dc_float_int_outputs[]={3,-3,0,0,2147483520,INT32_MIN,INT32_MIN,
                                             INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN};
#endif
