/* Independent desktop SSE rounding oracle, test only (AI-assisted). */
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>

uint32_t vu0_sse_lane(unsigned op, uint32_t a, uint32_t b, uint32_t c) {
  float x, y, z, result;
  __m128 left, right, value;
  unsigned saved = _mm_getcsr();
  uint32_t bits;
  memcpy(&x, &a, 4); memcpy(&y, &b, 4); memcpy(&z, &c, 4);
  _mm_setcsr(0x1f80u); /* RN-even, gradual underflow, masked exceptions. */
  left = _mm_set_ss(x); right = _mm_set_ss(y);
  if (op == 0) value = _mm_add_ss(left, right);
  else if (op == 1) value = _mm_sub_ss(left, right);
  else {
    value = _mm_mul_ss(left, right);
    if (op == 3) value = _mm_add_ss(_mm_set_ss(z), value);
  }
  result = _mm_cvtss_f32(value);
  memcpy(&bits, &result, 4);
  _mm_setcsr(saved);
  return bits;
}
