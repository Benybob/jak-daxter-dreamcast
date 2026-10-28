/* Scalar stand-in for the SSE intrinsics game/common/vu.h and the jak1 mips2c
 * functions use, for SH-4 builds only (AI-assisted), ISC. immintrin.h is
 * x86-only; each operation here is the same lane-wise arithmetic. */
#pragma once
typedef struct { float v[4]; } __m128;
static inline __m128 _mm_load_ps(const float *p){__m128 r;for(int i=0;i<4;++i)r.v[i]=p[i];return r;}
static inline void _mm_store_ps(float *p,__m128 a){for(int i=0;i<4;++i)p[i]=a.v[i];}
static inline __m128 _mm_set1_ps(float x){__m128 r;for(int i=0;i<4;++i)r.v[i]=x;return r;}
static inline __m128 _mm_add_ps(__m128 a,__m128 b){for(int i=0;i<4;++i)a.v[i]+=b.v[i];return a;}
static inline __m128 _mm_mul_ps(__m128 a,__m128 b){for(int i=0;i<4;++i)a.v[i]*=b.v[i];return a;}
static inline __m128 _mm_max_ps(__m128 a,__m128 b){for(int i=0;i<4;++i)a.v[i]=a.v[i]>b.v[i]?a.v[i]:b.v[i];return a;}
static inline __m128 _mm_min_ps(__m128 a,__m128 b){for(int i=0;i<4;++i)a.v[i]=a.v[i]<b.v[i]?a.v[i]:b.v[i];return a;}
#define _mm_blend_ps(a,b,m) __extension__({__m128 _a=(a),_b=(b);for(int _i=0;_i<4;++_i)if((m)>>_i&1)_a.v[_i]=_b.v[_i];_a;})
