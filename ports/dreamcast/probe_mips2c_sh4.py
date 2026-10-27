"""Probe: do the PC port's mips2c C++ sources compile for SH-4? (AI-assisted)

The PC port replaced the original hand-written MIPS assembly functions with C++
translations in game/mips2c. The native runtime currently answers every
__pc-get-mips2c lookup with a stub that faults (MEMORY_BUDGET.md). This probe runs
a syntax-only SH-4 compile (sh-elf-g++ -fsyntax-only) of every jak1 mips2c file
with exactly two adaptations, both generated into a temporary directory:

- a scalar stand-in for the 8 SSE intrinsics game/common/vu.h and the functions
  use (load/store/set1/add/mul/max/min/blend), since immintrin.h is x86-only;
- __forceinline defined as the Linux branch of vu.h's REALLY_INLINE.

It proves nothing about linking or execution. It reports which files parse.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

SHIM = '''/* Probe-only scalar stand-in for the SSE intrinsics mips2c uses (AI-assisted). */
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
'''


def main():
    port = Path(__file__).resolve().parent
    root = port.parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gxx', type=Path, default=port.parents[2] / 'dc-sdk/sh-elf/bin/sh-elf-g++.exe')
    args = parser.parse_args()
    files = sorted((root / 'game/mips2c/jak1_functions').glob('*.cpp'))
    clean = []
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / 'immintrin.h').write_text(SHIM)
        for f in files:
            r = subprocess.run([str(args.gxx), '-std=gnu++20', '-fsyntax-only', '-m4-single', '-ml', f'-I{tmp}',
                                f'-I{root}', f'-I{root / "third-party"}', f'-I{root / "third-party/fmt/include"}',
                                '-D__forceinline=__attribute__((always_inline))', str(f)],
                               capture_output=True, text=True)
            errors = [l for l in r.stderr.splitlines() if ' error' in l]
            if r.returncode == 0 and not errors:
                clean.append(f.name)
            else:
                print(f'FAIL {f.name}: {(errors or r.stderr.splitlines() or ["?"])[0][:200]}')
    print(f'{len(clean)} of {len(files)} jak1 mips2c files parse for SH-4')


if __name__ == '__main__':
    main()
