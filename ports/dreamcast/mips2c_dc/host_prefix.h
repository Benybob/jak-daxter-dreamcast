/* Host-oracle prefix (AI-assisted), ISC: MinGW's own __forceinline is
 * "extern __inline__"; vu.h's REALLY_INLINE expects only the attribute. The
 * system headers are included first, with MinGW's definition, then it changes. */
#pragma once
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <immintrin.h>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#ifdef __MINGW32__
#undef __forceinline
#define __forceinline __attribute__((always_inline))
#endif
