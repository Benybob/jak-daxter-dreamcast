/* mips2c differential cases, shared by the host oracle and the Flycast boot
 * (AI-assisted), ISC.
 *
 * Each case fills one zeroed region at a caller-chosen GOAL address, points the
 * symbols it needs into it, and calls mips2c functions by name through the
 * generated wrappers (float arguments as bits). The host oracle
 * (host_mips2c_oracle.cpp) runs the unchanged C++ the way desktop does; the boot
 * runs the native SH-4 build. Afterwards every aligned word that is a pointer into
 * the region becomes its offset, and #f (s7) becomes a marker, so both sides hash
 * the same bytes. Only functions whose paths stay in C++ (no jalr back into GOAL)
 * are called. The region is 16 KiB aligned on both sides, as the PS2 scratchpad
 * is, so SPAD offsets that are multiples of 0x4000 survive ori address forming. */
#ifndef DC_MIPS2C_CASES_H
#define DC_MIPS2C_CASES_H
#include <stdint.h>
#include <string.h>

#define DC_M2C_REGION 0x10000u
#define DC_M2C_FALSE_MARK 0xfffffff7u

typedef struct dc_m2c_env {
  unsigned char *region; /* DC_M2C_REGION bytes */
  uint32_t base;         /* the region's GOAL address */
  uint32_t s7;           /* #f */
  void (*set_symbol)(struct dc_m2c_env *, const char *name, uint32_t value);
  uint64_t (*call)(struct dc_m2c_env *, const char *name, const uint64_t *args8);
  /* The same call made the way translated code calls another mips2c function:
   * jalr on the GOAL stub (SH-4), desktop's trampoline (host). */
  uint64_t (*call_jalr)(struct dc_m2c_env *, const char *name, const uint64_t *args8);
  void *context;
} dc_m2c_env;

static inline void dc_m2c_f(dc_m2c_env *e, uint32_t at, float v) { memcpy(e->region + at, &v, 4); }
static inline void dc_m2c_u(dc_m2c_env *e, uint32_t at, uint32_t v) { memcpy(e->region + at, &v, 4); }
static inline void dc_m2c_b(dc_m2c_env *e, uint32_t at, uint8_t v) { e->region[at] = v; }
static inline void dc_m2c_vec(dc_m2c_env *e, uint32_t at, float x, float y, float z, float w) {
  dc_m2c_f(e, at, x); dc_m2c_f(e, at + 4, y); dc_m2c_f(e, at + 8, z); dc_m2c_f(e, at + 12, w);
}
static inline uint32_t dc_m2c_ptr(dc_m2c_env *e, uint32_t offset) { return e->base + offset; }
static inline uint64_t dc_m2c_fbits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
static inline uint64_t dc_m2c_call(dc_m2c_env *e, const char *name, uint64_t a0, uint64_t a1, uint64_t a2,
                                   uint64_t a3, uint64_t a4, uint64_t a5) {
  const uint64_t args[8] = {a0, a1, a2, a3, a4, a5, 0, 0};
  return e->call(e, name, args);
}
static inline uint64_t dc_m2c_call0(dc_m2c_env *e, const char *name) { return dc_m2c_call(e, name, 0, 0, 0, 0, 0, 0); }

/* A small deterministic generator for filler data (floats in [-8, 8)). */
static inline float dc_m2c_rand(uint32_t *state) {
  uint32_t x = *state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *state = x;
  return (float)(int32_t)(x >> 16 & 0xffffu) / 4096.0f - 8.0f;
}

/* A random xyz with the given w, in a fixed order (C leaves argument order unspecified). */
static inline void dc_m2c_rand_vec(dc_m2c_env *e, uint32_t at, uint32_t *state, float w) {
  float x = dc_m2c_rand(state), y = dc_m2c_rand(state), z = dc_m2c_rand(state);
  dc_m2c_vec(e, at, x, y, z, w);
}

/* ---- sky_tng: init-sky-regs, set-sky-vf27/vf23, render-sky-tri (clipping) ---- */
static inline uint32_t dc_m2c_sky(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, DATA = 0x0404, VF27 = 0x0600, VERTS = 0x0700, DMA = 0x0804, SPAD = 0x4000 };
  uint32_t base_after;
  dc_m2c_vec(e, CAMERA + 572, 1.0f, 0.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, CAMERA + 588, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, CAMERA + 604, 0.0f, 0.0f, 1.0f, 0.25f);
  dc_m2c_vec(e, CAMERA + 620, 0.0f, 0.0f, -8.0f, 1.0f);
  dc_m2c_vec(e, CAMERA + 700, 256.0f, 224.0f, 1.0f, 1.0f);
  dc_m2c_vec(e, CAMERA + 732, 2048.0f, 2048.0f, 16777000.0f, 1.0f);
  dc_m2c_f(e, CAMERA + 124, 0.5f); dc_m2c_f(e, CAMERA + 128, 0.75f);
  dc_m2c_f(e, CAMERA + 828, 3.0f); dc_m2c_f(e, CAMERA + 848, 1.0f);
  dc_m2c_vec(e, VF27, 0.125f, 0.25f, 0.5f, 1.0f);
  /* One vertex behind the near plane, so both hyperplane clippers run. */
  dc_m2c_vec(e, VERTS + 0, -1.0f, -1.0f, 4.0f, 1.0f);   dc_m2c_vec(e, VERTS + 16, 0.0f, 0.0f, 1.0f, 0.0f);
  dc_m2c_vec(e, VERTS + 32, 128.0f, 64.0f, 32.0f, 128.0f);
  dc_m2c_vec(e, VERTS + 48, 3.0f, -1.0f, 5.0f, 1.0f);   dc_m2c_vec(e, VERTS + 64, 1.0f, 0.0f, 1.0f, 0.0f);
  dc_m2c_vec(e, VERTS + 80, 64.0f, 128.0f, 32.0f, 128.0f);
  dc_m2c_vec(e, VERTS + 96, 0.5f, 2.0f, -3.0f, 1.0f);   dc_m2c_vec(e, VERTS + 112, 0.0f, 1.0f, 1.0f, 0.0f);
  dc_m2c_vec(e, VERTS + 128, 32.0f, 64.0f, 128.0f, 128.0f);
  dc_m2c_u(e, DMA + 0, 0x1000); dc_m2c_u(e, DMA + 4, dc_m2c_ptr(e, DMA + 12));
  dc_m2c_u(e, DMA + 8, dc_m2c_ptr(e, DMA + 12 + 0x1000));
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*sky-tng-data*", dc_m2c_ptr(e, DATA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_call0(e, "init-sky-regs");
  dc_m2c_call(e, "set-sky-vf27", dc_m2c_ptr(e, VF27), 0, 0, 0, 0, 0);
  dc_m2c_call(e, "set-sky-vf23-value", 0x12345678u, 0, 0, 0, 0, 0);
  dc_m2c_call(e, "render-sky-tri", dc_m2c_ptr(e, VERTS), dc_m2c_ptr(e, DMA), 0, 0, 0, 0);
  memcpy(&base_after, e->region + DMA + 4, 4);
  return base_after - dc_m2c_ptr(e, DMA + 12);  /* packet bytes written */
}

/* ---- ripple: ripple-execute-init, ripple-apply-wave-table, ripple-matrix-scale ----
 * create-wave-table calls GOAL's sin/cos/atan and is left to gameplay. */
static inline uint32_t dc_m2c_ripple(dc_m2c_env *e) {
  enum { POLY = 0x0010, SPAD = 0x4000, ARGS = 0x0100, HEADERS = 0x0200, INDICES = 0x0300, COUNTS = 0x0400,
         DEST = 0x1000, SRC = 0x2000, DST = 0x3000 };
  uint32_t i, state = 0x2468ace1u;
  dc_m2c_vec(e, POLY, -0.49999788f, 0.041664604f, -0.0013868269f, 0.000023717716f);
  e->set_symbol(e, "*cos-poly-vec*", dc_m2c_ptr(e, POLY));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_call0(e, "ripple-execute-init");  /* fills ripple's VU0 table (read by create-wave-table) */
  /* apply-wave-table: two entries of 5 and 3 vertex pairs, a 256-entry byte table. */
  for (i = 0; i < 1024; ++i) dc_m2c_b(e, SPAD + 1024 + i, (uint8_t)(i * 37u + 11u));
  dc_m2c_u(e, ARGS + 0, dc_m2c_ptr(e, DEST)); dc_m2c_u(e, ARGS + 4, dc_m2c_ptr(e, HEADERS));
  dc_m2c_u(e, ARGS + 8, dc_m2c_ptr(e, INDICES)); dc_m2c_u(e, ARGS + 12, dc_m2c_ptr(e, COUNTS));
  dc_m2c_u(e, ARGS + 16, 2u << 16);  /* u16 entry count at +18 */
  dc_m2c_b(e, HEADERS + 0, 4); dc_m2c_b(e, HEADERS + 1, 2); dc_m2c_b(e, HEADERS + 2, 1); dc_m2c_b(e, HEADERS + 3, 0);
  dc_m2c_b(e, HEADERS + 4, 3); dc_m2c_b(e, HEADERS + 5, 5); dc_m2c_b(e, HEADERS + 6, 2); dc_m2c_b(e, HEADERS + 7, 1);
  dc_m2c_b(e, COUNTS + 0, 5); dc_m2c_b(e, COUNTS + 2, 3);
  for (i = 0; i < 32; ++i) dc_m2c_b(e, INDICES + i, (uint8_t)(i * 29u + 3u));
  dc_m2c_call(e, "ripple-apply-wave-table", dc_m2c_ptr(e, ARGS), 0, 0, 0, 0, 0);
  /* matrix-scale: three 128-byte records, three scale factors as float bits. */
  for (i = 0; i < 3 * 128; i += 4) dc_m2c_f(e, SRC + i, dc_m2c_rand(&state));
  dc_m2c_call(e, "ripple-matrix-scale", dc_m2c_ptr(e, SRC), 3, dc_m2c_fbits(0.5f), dc_m2c_fbits(1.25f),
              dc_m2c_fbits(-2.0f), dc_m2c_ptr(e, DST));
  return 0;
}

/* ---- collide_mesh: methods 14 and 15 (vertices through one and two matrices) ---- */
static inline uint32_t dc_m2c_collide_mesh(dc_m2c_env *e) {
  enum { MESH = 0x0104, VERTS = 0x0400, M1 = 0x0800, M2 = 0x0880, OUT14 = 0x1000, OUT15 = 0x2000 };
  uint32_t i, state = 0x13579bdfu;
  dc_m2c_u(e, MESH + 8, 10); dc_m2c_u(e, MESH + 12, dc_m2c_ptr(e, VERTS));
  for (i = 0; i < 10; ++i) {
    /* Separate statements: C leaves the order of argument evaluation open. */
    float x = dc_m2c_rand(&state) * 8.0f, y = dc_m2c_rand(&state) * 8.0f, z = dc_m2c_rand(&state) * 8.0f;
    dc_m2c_vec(e, VERTS + 16 * i, x, y, z, 1.0f);
  }
  dc_m2c_vec(e, M1 + 0, 0.8f, 0.0f, -0.6f, 0.0f); dc_m2c_vec(e, M1 + 16, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, M1 + 32, 0.6f, 0.0f, 0.8f, 0.0f); dc_m2c_vec(e, M1 + 48, 12.5f, -3.0f, 40.0f, 1.0f);
  dc_m2c_vec(e, M2 + 0, 2.0f, 0.5f, 0.0f, 0.0f); dc_m2c_vec(e, M2 + 16, -0.5f, 2.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, M2 + 32, 0.0f, 0.0f, 2.0f, 0.0f); dc_m2c_vec(e, M2 + 48, 100.0f, 50.0f, -25.0f, 1.0f);
  dc_m2c_call(e, "(method 14 collide-mesh)", dc_m2c_ptr(e, MESH), dc_m2c_ptr(e, M1), dc_m2c_ptr(e, OUT14), 0, 0, 0);
  dc_m2c_call(e, "(method 15 collide-mesh)", dc_m2c_ptr(e, MESH), dc_m2c_ptr(e, M1), dc_m2c_ptr(e, M2),
              dc_m2c_ptr(e, OUT15), 0, 0);
  return 0;
}

/* ---- collide_edge_grab: methods 16 and 15 of collide-edge-work, then
 * add-to-list! (method 10 of collide-edge-hold-list) ---- */
static inline uint32_t dc_m2c_edge_grab(dc_m2c_env *e) {
  enum { WORK = 0x1000, CACHE = 0x4004, TRIS = CACHE + 4908, ITEMS = 0x6000, SHAPE = 0x7004, HOLD = WORK + 7808 };
  /* Triangles: up-facing (kept), down-facing and steep (rejected), one with a mode bit set. */
  static const float tri[8][9] = {
    {0, 0, 0, 0, 0, 1, 1, 0, 0},     {1, 0, 0, 1, 0, 1, 2, 0, 0},       {0, 0, 1, 0, 0, 2, 1, 0, 1},
    {0, 0, 0, 1, 0, 0, 0, 0, 1},     {0, 0, 0, 0, 1, 0, 1, 0, 0},       {3, 0.5f, 3, 3, 0.2f, 4, 4, 0.1f, 3},
    {-2, 1, -2, -2, 1, -1, -1, 1, -2}, {5, 0, 5, 5, 0, 6, 6, 0, 5}};
  static const uint32_t pat[8] = {0, 0, 0, 0, 0, 0, 0, 8};
  static const float keys[4] = {3.0f, 1.0f, 2.0f, 5.0f};
  uint32_t i, k, count;
  dc_m2c_u(e, WORK + 0, dc_m2c_ptr(e, CACHE));
  dc_m2c_u(e, WORK + 4, dc_m2c_ptr(e, SHAPE));          /* cshape: method 15 reads its vector at +12 */
  dc_m2c_vec(e, SHAPE + 12, 0.5f, 0.25f, 0.5f, 1.0f);
  dc_m2c_vec(e, WORK + 64, -1.0f, -1.0f, -1.0f, 1.0f);  /* within-reach-box */
  dc_m2c_vec(e, WORK + 80, 8.0f, 2.0f, 8.0f, 1.0f);
  for (k = 0; k < 4; ++k) { dc_m2c_u(e, WORK + 96 + 4 * k, (uint32_t)-1000); dc_m2c_u(e, WORK + 112 + 4 * k, 1000); }
  dc_m2c_vec(e, WORK + 128, 0.5f, 0.0f, 0.5f, 1.0f);    /* search-pt */
  dc_m2c_vec(e, WORK + 144, 0.0f, 0.0f, 1.0f, 0.0f);    /* search-dir-vec */
  dc_m2c_f(e, WORK + 160, 100.0f); dc_m2c_f(e, WORK + 164, 0.5f);
  dc_m2c_u(e, CACHE + 0, 8);
  for (i = 0; i < 8; ++i) {
    for (k = 0; k < 3; ++k)
      dc_m2c_vec(e, TRIS + 64 * i + 16 * k, tri[i][3 * k], tri[i][3 * k + 1], tri[i][3 * k + 2], 1.0f);
    dc_m2c_u(e, TRIS + 64 * i + 48, pat[i]);
  }
  dc_m2c_call(e, "(method 16 collide-edge-work)", dc_m2c_ptr(e, WORK), 0, 0, 0, 0, 0);
  dc_m2c_call(e, "(method 15 collide-edge-work)", dc_m2c_ptr(e, WORK), 0, 0, 0, 0, 0);
  {uint32_t verts, edges;
   memcpy(&count, e->region + WORK + 16, 4); memcpy(&verts, e->region + WORK + 8, 4);
   memcpy(&edges, e->region + WORK + 12, 4);
   count |= verts << 8 | edges << 16;}
  /* An empty hold list (head #f), then four items inserted in key order. */
  dc_m2c_u(e, HOLD + 4, 0); dc_m2c_u(e, HOLD + 8, e->s7);
  for (i = 0; i < 4; ++i) {
    dc_m2c_u(e, ITEMS + 48 * i, e->s7); dc_m2c_f(e, ITEMS + 48 * i + 4, keys[i]);
    dc_m2c_vec(e, ITEMS + 48 * i + 16, (float)i, keys[i], -(float)i, 1.0f);
    dc_m2c_call(e, "(method 10 collide-edge-hold-list)", dc_m2c_ptr(e, HOLD), dc_m2c_ptr(e, ITEMS + 48 * i),
                0, 0, 0, 0);
  }
  return count;  /* triangles method 16 kept | method 15's vertices << 8 | edges << 16 */
}

/* ---- generic_effect: generic-light-proc (index stream -> lit colors) ----
 * The two prepare-dma functions are reached through mercneric-convert. */
static inline uint32_t dc_m2c_generic_light(dc_m2c_env *e) {
  enum { SPAD = 0x4000, STREAM = 0x0100, INDICES = 0x0200, TABLE = 0x0400, OUT1 = 0x1000, OUT2 = 0x2000 };
  uint32_t i, state = 0x0badf00du;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_u(e, SPAD + 60, dc_m2c_ptr(e, STREAM));
  dc_m2c_u(e, SPAD + 52, 8);  /* vertices */
  dc_m2c_u(e, SPAD + 20, dc_m2c_ptr(e, OUT1)); dc_m2c_u(e, SPAD + 24, dc_m2c_ptr(e, OUT1 + 0x400));
  dc_m2c_u(e, SPAD + 28, dc_m2c_ptr(e, OUT2));
  dc_m2c_u(e, STREAM + 0, dc_m2c_ptr(e, INDICES)); dc_m2c_u(e, STREAM + 4, dc_m2c_ptr(e, TABLE));
  for (i = 0; i < 16; ++i) {
    uint16_t index = (uint16_t)(((i * 7u) & 15u) | ((i & 1u) << 8));
    memcpy(e->region + INDICES + 2 * i, &index, 2);
  }
  for (i = 0; i < 16 * 32; ++i) dc_m2c_b(e, TABLE + i, (uint8_t)(i * 13u + 7u));
  /* Light matrix rows, colors and ambient (12688..12784), and the clamp (12144). */
  for (i = 0; i < 7; ++i) {
    float x = dc_m2c_rand(&state) / 64.0f, y = dc_m2c_rand(&state) / 64.0f, z = dc_m2c_rand(&state) / 64.0f;
    dc_m2c_vec(e, SPAD + 12688 + 16 * i, x, y, z, 1.0f);
  }
  dc_m2c_vec(e, SPAD + 12144, 255.0f, 255.0f, 255.0f, 255.0f);
  dc_m2c_call0(e, "generic-light-proc");
  return 0;
}

/* ---- generic_merc: high-speed-reject (bounding spheres against two planes) ----
 * The rest of the file runs through mercneric-convert's GOAL callbacks. */
static inline uint32_t dc_m2c_high_speed_reject(dc_m2c_env *e) {
  enum { SPAD = 0x4000, CTRL = 0x0100 };
  uint32_t i, k, result = 0, state = 0x5eed1234u;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_u(e, SPAD + 7308, dc_m2c_ptr(e, CTRL));
  dc_m2c_u(e, CTRL + 20, 8);  /* u16 record count: the loop evaluates whole groups of four */
  for (k = 0; k < 3; ++k) {
    /* Three plane sets: all records outside, all inside, mixed. */
    const float offset = k == 0 ? -1000.0f : k == 1 ? 1000.0f : 0.0f;
    dc_m2c_vec(e, SPAD + 7360, 0.25f, 0.5f, 0.75f, 1.0f);
    dc_m2c_vec(e, SPAD + 7376, offset, offset + (k == 2 ? 3.0f : 0.0f), offset - (k == 2 ? 2.0f : 0.0f), offset);
    for (i = 0; i < 16; ++i) {  /* the pipelined loop reads one group past the count */
      float x = dc_m2c_rand(&state), y = dc_m2c_rand(&state), z = dc_m2c_rand(&state);
      dc_m2c_vec(e, CTRL + 448 + 32 * i, (x < 0 ? -x : x) + 0.5f, (y < 0 ? -y : y) + 0.5f, (z < 0 ? -z : z) + 0.5f,
                 1.0f);
      dc_m2c_u(e, CTRL + 448 + 32 * i + 8, (k == 2 && i == 3) ? 0x80000000u : 0u);
    }
    dc_m2c_b(e, SPAD + 6773, 1);
    dc_m2c_call0(e, "high-speed-reject");
    result = result << 8 | e->region[SPAD + 6773];
  }
  return result;  /* the reject byte after each plane set */
}

/* ---- bones: draw-bones-check-longest-edge-asm (a float argument) ----
 * bones-mtx-calc follows pointers inside its joint records; it needs real joints. */
static inline uint32_t dc_m2c_bones(dc_m2c_env *e) {
  enum { SPAD = 0x4000, CAMERA = 0x0104, DRAW = 0x0404 };
  static const float radius[4] = {0.5f, 4.0f, 40.0f, 400.0f};
  uint32_t i, result = 0;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  for (i = 0; i < 32; ++i) dc_m2c_f(e, CAMERA + 4 * i, 1.0f + 0.5f * (float)i);  /* fov, clip planes, ... */
  dc_m2c_f(e, DRAW + 64, 12.0f); dc_m2c_f(e, DRAW + 136, 3.0f);
  dc_m2c_f(e, SPAD + 176, 2.0f); dc_m2c_f(e, SPAD + 180, -1.5f); dc_m2c_f(e, SPAD + 184, 30.0f);
  for (i = 0; i < 4; ++i) {
    uint64_t r = dc_m2c_call(e, "draw-bones-check-longest-edge-asm", dc_m2c_ptr(e, DRAW), dc_m2c_fbits(radius[i]),
                             0, 0, 0, 0);
    result = result << 1 | (uint32_t)((uint32_t)r != e->s7);
  }
  return result;  /* one bit per radius: the check's answer was not #f */
}

/* ---- generic_effect2: generic-no-light-dproc (the light-proc work layout) ---- */
static inline uint32_t dc_m2c_generic_no_light(dc_m2c_env *e) {
  enum { SPAD = 0x4000, STREAM = 0x0100, INDICES = 0x0200, TABLE = 0x0400, OUT = 0x1000 };
  uint32_t i;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_u(e, SPAD + 60, dc_m2c_ptr(e, STREAM));
  dc_m2c_u(e, SPAD + 52, 8);
  for (i = 0; i < 5; ++i) dc_m2c_u(e, SPAD + 20 + 4 * i, dc_m2c_ptr(e, OUT + 0x400 * i));
  dc_m2c_u(e, STREAM + 0, dc_m2c_ptr(e, INDICES)); dc_m2c_u(e, STREAM + 4, dc_m2c_ptr(e, TABLE));
  for (i = 0; i < 16; ++i) {
    uint16_t index = (uint16_t)(((i * 5u + 3u) & 15u) | ((i & 2u) << 7));
    memcpy(e->region + INDICES + 2 * i, &index, 2);
  }
  for (i = 0; i < 16 * 32; ++i) dc_m2c_b(e, TABLE + i, (uint8_t)(i * 11u + 5u));
  dc_m2c_vec(e, SPAD + 12160, 128.0f, 128.0f, 128.0f, 128.0f);
  dc_m2c_call0(e, "generic-no-light-dproc");
  return 0;
}

/* ---- ocean: init-ocean-far-regs, render-ocean-quad (tail-calls draw-large-polygon-ocean
 * and sky_tng's hyperplane clippers directly, in C++) ---- */
static inline uint32_t dc_m2c_ocean(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, DATA = 0x0404, QUAD = 0x0500, DMA = 0x0804, SPAD = 0x4000 };
  static const float corner[2][4][3] = {
    {{-1.0f, -1.0f, 4.0f}, {1.0f, -1.0f, 4.0f}, {1.0f, 1.0f, 5.0f}, {-1.0f, 1.0f, 5.0f}},
    /* two corners behind the near plane, so both hyperplane clippers cut */
    {{-2.0f, -1.0f, 6.0f}, {3.0f, -1.0f, -3.0f}, {2.0f, 2.0f, -2.0f}, {-1.0f, 1.5f, 3.0f}}};
  uint32_t q, v, result = 0, before, after;
  dc_m2c_vec(e, CAMERA + 572, 1.0f, 0.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, CAMERA + 588, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, CAMERA + 604, 0.0f, 0.0f, 1.0f, 0.25f);
  dc_m2c_vec(e, CAMERA + 620, 0.0f, 0.0f, -8.0f, 1.0f);
  dc_m2c_vec(e, CAMERA + 700, 256.0f, 224.0f, 1.0f, 1.0f);
  dc_m2c_vec(e, CAMERA + 716, 2048.0f, 2048.0f, 0.5f, 0.0f);
  dc_m2c_vec(e, CAMERA + 732, 2048.0f, 2048.0f, 16777000.0f, 1.0f);
  dc_m2c_f(e, CAMERA + 124, 0.5f); dc_m2c_f(e, CAMERA + 128, 0.75f); dc_m2c_f(e, CAMERA + 828, 3.0f);
  dc_m2c_vec(e, DATA + 44, 0.125f, 0.25f, 0.5f, 1.0f);
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*sky-tng-data*", dc_m2c_ptr(e, DATA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_u(e, DMA + 4, dc_m2c_ptr(e, DMA + 12)); dc_m2c_u(e, DMA + 8, dc_m2c_ptr(e, DMA + 12 + 0x1000));
  dc_m2c_call0(e, "init-ocean-far-regs");
  for (q = 0; q < 2; ++q) {
    uint32_t quad = QUAD + 0x100 * q;
    for (v = 0; v < 4; ++v) {
      dc_m2c_vec(e, quad + 48 * v, corner[q][v][0], corner[q][v][1], corner[q][v][2], 1.0f);
      dc_m2c_vec(e, quad + 48 * v + 16, (float)(v & 1), (float)(v >> 1), 1.0f, 0.0f);
      dc_m2c_vec(e, quad + 48 * v + 32, 32.0f * (float)(v + 1), 64.0f, 128.0f - 16.0f * (float)v, 128.0f);
    }
    memcpy(&before, e->region + DMA + 4, 4);
    result = result << 1 |
             (uint32_t)((uint32_t)dc_m2c_call(e, "render-ocean-quad", dc_m2c_ptr(e, quad), dc_m2c_ptr(e, DMA), 0, 0, 0, 0) != e->s7);
    memcpy(&after, e->region + DMA + 4, 4);
    result = result << 12 | ((after - before) & 0xfffu);
  }
  return result;  /* per quad: drawn bit, packet bytes */
}

/* ---- collide_probe: collide-probe-node over a two-level draw-node tree ---- */
static inline uint32_t dc_m2c_probe_node(dc_m2c_env *e) {
  enum { WORK = 0x0100, STACK = 0x0200, OUT = 0x0300, ROOT = 0x0804, CHILD = 0x0a04, LEAF = 0x0c04, ITEMS = 0x8000 };
  static const float root[4][4] = {{10, 10, 10, 5}, {500, 0, 0, 5}, {-50, 20, 0, 30}, {0, -98, 0, 1}};
  static const uint8_t root_count[4] = {4, 4, 3, 2}, root_flag[4] = {0, 0, 1, 0};
  uint32_t state = 0x1234567u, n, k, leaves = 0, count;
  dc_m2c_vec(e, WORK + 48, 1.0f, 0.0f, 0.0f, 0.0f); dc_m2c_vec(e, WORK + 64, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, WORK + 80, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, WORK + 96, 5.0f, 0.0f, 0.0f, 1.0f);
  for (k = 0; k < 3; ++k) { dc_m2c_u(e, WORK + 16 + 4 * k, (uint32_t)-100); dc_m2c_u(e, WORK + 32 + 4 * k, 100); }
  e->set_symbol(e, "*collide-work*", dc_m2c_ptr(e, WORK));
  e->set_symbol(e, "*collide-probe-stack*", dc_m2c_ptr(e, STACK));
  for (n = 0; n < 4; ++n) {
    uint32_t node = ROOT + 32 * n, array;
    dc_m2c_vec(e, node + 12, root[n][0], root[n][1], root[n][2], root[n][3]);
    dc_m2c_b(e, node + 2, root_count[n]); dc_m2c_b(e, node + 3, root_flag[n]);
    if (root_flag[n]) {  /* an array of nodes, each with four leaves */
      array = CHILD;
      for (k = 0; k < root_count[n]; ++k) {
        uint32_t child = CHILD + 32 * k;
        dc_m2c_vec(e, child + 12, -50.0f + 20.0f * (float)k, 20.0f, 0.0f, 12.0f);
        dc_m2c_b(e, child + 2, 4); dc_m2c_b(e, child + 3, 0);
        dc_m2c_u(e, child + 4, dc_m2c_ptr(e, LEAF + 128 * leaves / 4));
        leaves += 4;
      }
    } else {
      array = LEAF + 128 * leaves / 4;
      leaves += 4;
    }
    dc_m2c_u(e, node + 4, dc_m2c_ptr(e, array));
  }
  for (k = 0; k < leaves; ++k) {  /* leaves: an item pointer and a sphere in [-150, 150) */
    uint32_t leaf = LEAF + 32 * k;
    float x = dc_m2c_rand(&state) * 18.75f, y = dc_m2c_rand(&state) * 18.75f, z = dc_m2c_rand(&state) * 18.75f;
    dc_m2c_vec(e, leaf + 12, x, y, z, 4.0f + (float)(k % 5));
    dc_m2c_u(e, leaf + 4, dc_m2c_ptr(e, ITEMS + 16 * k));
  }
  dc_m2c_call(e, "collide-probe-node", dc_m2c_ptr(e, ROOT), 4, dc_m2c_ptr(e, OUT), 0, 0, 0);
  memcpy(&count, e->region + OUT, 4);
  return count;  /* leaves whose spheres touch the box */
}

/* ---- generic_tie: generic-tie-dma-to-spad-sync (cnt, ref, next, refe tags) ---- */
static inline uint32_t dc_m2c_tie_dma(dc_m2c_env *e) {
  enum { SPAD = 0x4000, CHAIN = 0x0800, REFDATA = 0x0900, NEXT = 0x0a00, REFE = 0x0b00 };
  uint32_t i;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  for (i = 0; i < 0x400; ++i) dc_m2c_b(e, CHAIN + i, (uint8_t)(i * 13u + 1u));
  dc_m2c_u(e, CHAIN + 0, 1u << 28 | 2u); dc_m2c_u(e, CHAIN + 4, 0);             /* cnt, 2 qw */
  dc_m2c_u(e, CHAIN + 48, 3u << 28 | 1u); dc_m2c_u(e, CHAIN + 52, dc_m2c_ptr(e, REFDATA));  /* ref, 1 qw */
  dc_m2c_u(e, CHAIN + 64, 2u << 28 | 1u); dc_m2c_u(e, CHAIN + 68, dc_m2c_ptr(e, NEXT));     /* next, 1 qw */
  dc_m2c_u(e, NEXT + 0, 0u << 28 | 2u); dc_m2c_u(e, NEXT + 4, dc_m2c_ptr(e, REFE));         /* refe, 2 qw */
  dc_m2c_call(e, "generic-tie-dma-to-spad-sync", dc_m2c_ptr(e, CHAIN), 0x10100u, 0, 0, 0, 0);
  return 0;
}

/* ---- joint: cspace<-parented-transformq-joint! (unscaled and scaled parents) ---- */
static inline uint32_t dc_m2c_joint(dc_m2c_env *e) {
  enum { PARENT = 0x0100, CSPACE = 0x0140, PBONE = 0x0200, BONE = 0x0300, TQ = 0x0400 };
  uint32_t pass, i, state = 0xbeef01u;
  for (pass = 0; pass < 2; ++pass) {
    uint32_t pbone = PBONE + 0x400 * pass, bone = BONE + 0x400 * pass, tq = TQ + 0x400 * pass;
    uint32_t parent = PARENT + 0x400 * pass, cspace = CSPACE + 0x400 * pass;
    for (i = 0; i < 16; ++i) dc_m2c_f(e, pbone + 4 * i, (i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.125f);
    dc_m2c_f(e, pbone + 64, 1.0f + 0.5f * (float)pass); dc_m2c_f(e, pbone + 68, 2.0f); dc_m2c_f(e, pbone + 72, 0.75f);
    dc_m2c_u(e, pbone + 76, pass);  /* scaled parent */
    dc_m2c_u(e, parent + 16, dc_m2c_ptr(e, pbone));
    dc_m2c_u(e, cspace + 0, dc_m2c_ptr(e, parent)); dc_m2c_u(e, cspace + 16, dc_m2c_ptr(e, bone));
    dc_m2c_vec(e, tq + 0, 1.5f, -2.0f, 0.25f, 1.0f);
    dc_m2c_vec(e, tq + 16, 0.1f, 0.2f, 0.3f, 0.92736185f);
    dc_m2c_vec(e, tq + 32, 1.0f, 1.25f, 0.5f, 1.0f);
    dc_m2c_call(e, "cspace<-parented-transformq-joint!", dc_m2c_ptr(e, cspace), dc_m2c_ptr(e, tq), 0, 0, 0, 0);
  }
  return 0;
}

/* ---- merc_blend_shape: setup-blerc-chains-for-one-fragment (a short and a split fragment) ---- */
static inline uint32_t dc_m2c_blerc_chains(dc_m2c_env *e) {
  enum { SPAD = 0x4000, GLOBALS = 0x0100, WEIGHTS = 0x0200, HEADER = 0x0300, OUT = 0x1000 };
  static const int16_t weight[3] = {100, 0, -50};
  static const uint8_t header[2][5] = {{40, 0, 1, 1, 1}, {120, 0, 1, 0, 1}};
  uint32_t pass, result = 0;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*blerc-globals*", dc_m2c_ptr(e, GLOBALS));
  memcpy(e->region + WEIGHTS, weight, sizeof weight);
  for (pass = 0; pass < 2; ++pass) {
    uint32_t out = OUT + 0x1000 * pass, end;
    memcpy(e->region + HEADER + 16 * pass, header[pass], 5);
    {
      const uint64_t args[8] = {3, dc_m2c_ptr(e, WEIGHTS), dc_m2c_ptr(e, out), dc_m2c_ptr(e, 0x3000 + 0x400 * pass),
                                dc_m2c_ptr(e, HEADER + 16 * pass), dc_m2c_ptr(e, 0x5000 + 0x800 * pass), 0, 0};
      end = (uint32_t)e->call(e, "setup-blerc-chains-for-one-fragment", args);
    }
    result = result << 16 | ((end - dc_m2c_ptr(e, out)) & 0xffffu);
  }
  return result;  /* chain bytes per fragment */
}

/* ---- ocean_vu0: ocean-interp-wave (frames 0/1, and 5/6 by moving the frame table) ---- */
static inline uint32_t dc_m2c_ocean_wave(dc_m2c_env *e) {
  enum { WORK = 0x0104, FRAMES = 0x2000, OUT = 0x6000 };
  uint32_t i;
  e->set_symbol(e, "*ocean-work*", dc_m2c_ptr(e, WORK));
  for (i = 0; i < 0x800; ++i) dc_m2c_b(e, FRAMES + i, (uint8_t)(i * 7u + 3u));
  e->set_symbol(e, "*ocean-wave-frames*", dc_m2c_ptr(e, FRAMES));
  dc_m2c_call(e, "ocean-interp-wave", dc_m2c_ptr(e, OUT), 13, 0, 0, 0, 0);
  e->set_symbol(e, "*ocean-wave-frames*", dc_m2c_ptr(e, FRAMES) - 5u * 1024u);
  dc_m2c_call(e, "ocean-interp-wave", dc_m2c_ptr(e, OUT + 0x1000), 5 * 32 + 31, 0, 0, 0, 0);
  return 0;
}

/* ---- shadow: shadow-init-vars, shadow-xform-verts, shadow-calc-dual-verts, as
 * shadow-execute chains them (it also calls flush-cache and camera-pos in GOAL) ---- */
static inline uint32_t dc_m2c_shadow(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, GEO = 0x2000, WORK = 0x3000, OUT = 0x5000, SPAD = 0x8000 };
  static const uint8_t joints[12] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0};
  uint32_t i, state = 0x5eed5u;
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  for (i = 0; i < 16; ++i) dc_m2c_f(e, CAMERA + 364 + 4 * i, (i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.0625f);
  dc_m2c_vec(e, WORK + 64, 0.0f, -1.0f, 0.25f, 0.0f);
  dc_m2c_vec(e, WORK + 80, 0.0f, 1.0f, 0.0f, -2.0f);
  dc_m2c_vec(e, WORK + 96, 0.5f, 0.5f, 0.5f, 1.0f);
  dc_m2c_vec(e, WORK + 128, 1.0f, 2.0f, 3.0f, 1.0f);
  dc_m2c_u(e, WORK + 16, dc_m2c_ptr(e, OUT) - 9u);  /* rounded up to 16 */
  dc_m2c_u(e, GEO + 0, 0); dc_m2c_u(e, GEO + 4, 0);
  dc_m2c_u(e, GEO + 8, 6u | 2u << 16);  /* six verts, two of them double */
  dc_m2c_u(e, GEO + 20, 0x200); dc_m2c_u(e, GEO + 24, 0x300);
  for (i = 0; i < 2 * 32; ++i)  /* two joints' 128-byte records: a matrix first */
    dc_m2c_f(e, GEO + 144 + 4 * i, (i % 32 < 16) ? ((i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.25f) : 0.0f);
  for (i = 0; i < 6; ++i) dc_m2c_rand_vec(e, GEO + 0x200 + 16 * i, &state, 1.0f);
  memcpy(e->region + GEO + 0x300, joints, sizeof joints);
  dc_m2c_call(e, "shadow-init-vars", dc_m2c_ptr(e, GEO), dc_m2c_ptr(e, WORK), 0, 0, 0, 0);
  dc_m2c_call(e, "shadow-xform-verts", dc_m2c_ptr(e, GEO), dc_m2c_ptr(e, WORK), 0, 0, 0, 0);
  dc_m2c_call(e, "shadow-calc-dual-verts", dc_m2c_ptr(e, GEO), dc_m2c_ptr(e, WORK), 0, 0, 0, 0);
  return 0;
}

/* ---- tfrag: stats-tfrag-asm over fragments of each visibility/LOD kind ---- */
static inline uint32_t dc_m2c_tfrag_stats(dc_m2c_env *e) {
  enum { REGS = 0x0100, WORK = 0x0300, STATS = 0x0400, DMA = 0x0480, FRAGS = 0x0504 };
  uint32_t i, state = 0x7f4a7c15u, result = 0;
  for (i = 0; i < 31 * 4; ++i) dc_m2c_f(e, REGS + 4 * i, dc_m2c_rand(&state));
  dc_m2c_vec(e, WORK + 80, 1.0f, 1.0f, 1.0f, 1.0f);
  dc_m2c_u(e, WORK + 144, dc_m2c_ptr(e, STATS)); dc_m2c_u(e, WORK + 148, dc_m2c_ptr(e, STATS + 32));
  for (i = 0; i < 16; ++i) dc_m2c_b(e, DMA + i, (uint8_t)(3 * i + 2));
  e->set_symbol(e, "*transform-regs*", dc_m2c_ptr(e, REGS));
  e->set_symbol(e, "*tfrag-work*", dc_m2c_ptr(e, WORK));
  for (i = 0; i < 8; ++i) {
    uint32_t frag = FRAGS + 64 * i;
    dc_m2c_u(e, frag + 4, i == 7 ? 0 : dc_m2c_ptr(e, DMA));
    dc_m2c_rand_vec(e, frag + 12, &state, 1.0f + (float)i);
    dc_m2c_b(e, frag + 49, (uint8_t)(i & 1)); dc_m2c_b(e, frag + 50, (uint8_t)(i >> 1 & 1));
    dc_m2c_b(e, frag + 51, (uint8_t)(i >> 2 & 1));
    dc_m2c_call(e, "stats-tfrag-asm", dc_m2c_ptr(e, frag), 0, 0, 0, 0, 0);
  }
  for (i = 0; i < 2; ++i) {
    uint16_t n;
    memcpy(&n, e->region + STATS + 32 * i + 2, 2);
    result = result << 8 | n;
  }
  return result;  /* fragments counted in each stats record */
}

/* ---- collide_func: moving-sphere-triangle-intersect, called through jalr on its
 * stub (float argument and result as GPR bits). Face hits only: an edge hit
 * calls collide-do-primitives, which calls GOAL. ---- */
static inline uint32_t dc_m2c_sphere_triangle(dc_m2c_env *e) {
  enum { SPHERE = 0x0100, VELOCITY = 0x0200, TRI = 0x0300, POS = 0x0400, NORMAL = 0x0500, RESULT = 0x0600 };
  static const float velocity[3][3] = {{0.0f, 0.0f, -20.0f}, {0.5f, 0.25f, -8.0f}, {0.0f, 0.0f, 20.0f}};
  uint32_t i, result = 0;
  dc_m2c_vec(e, SPHERE, 0.25f, 0.5f, 10.0f, 1.0f);
  dc_m2c_vec(e, TRI + 0, -5.0f, -5.0f, 0.0f, 1.0f);
  dc_m2c_vec(e, TRI + 16, 5.0f, -5.0f, 0.0f, 1.0f);
  dc_m2c_vec(e, TRI + 32, 0.0f, 5.0f, 0.0f, 1.0f);
  for (i = 0; i < 3; ++i) {
    uint64_t r;
    dc_m2c_vec(e, VELOCITY + 16 * i, velocity[i][0], velocity[i][1], velocity[i][2], 0.0f);
    {
      const uint64_t args[8] = {dc_m2c_ptr(e, SPHERE), dc_m2c_ptr(e, VELOCITY + 16 * i), dc_m2c_fbits(1.0f),
                                dc_m2c_ptr(e, TRI), dc_m2c_ptr(e, POS + 16 * i), dc_m2c_ptr(e, NORMAL + 16 * i), 0, 0};
      r = e->call_jalr(e, "moving-sphere-triangle-intersect", args);
    }
    dc_m2c_u(e, RESULT + 4 * i, (uint32_t)r);
    result = result << 1 | (uint32_t)((uint32_t)r != 0xccbebc20u);  /* -1e8: no hit */
  }
  return result;
}

/* ---- texture: adgif-shader<-texture-with-update! (mip counts below and from 5, and
 * both byte-7 paths) ---- */
static inline uint32_t dc_m2c_adgif_texture(dc_m2c_env *e) {
  enum { TEXTURES = 0x0104, SHADERS = 0x1000 };
  static const uint8_t mips[4] = {1, 4, 5, 7}, byte7[4] = {1, 2, 1, 3};
  uint32_t i, k, result = 0;
  for (i = 0; i < 4; ++i) {
    uint32_t tex = TEXTURES + 0x100 * i, shader = SHADERS + 0x100 * i;
    uint16_t w = (uint16_t)(16u << i), h = (uint16_t)(256u >> i);
    memcpy(e->region + tex + 0, &w, 2); memcpy(e->region + tex + 2, &h, 2);
    dc_m2c_b(e, tex + 4, mips[i]); dc_m2c_b(e, tex + 5, (uint8_t)(i + 1));
    dc_m2c_b(e, tex + 6, (uint8_t)(3 * i)); dc_m2c_b(e, tex + 7, byte7[i]);
    for (k = 0; k < 8; ++k) {  /* the mip levels' destinations and widths */
      uint16_t v = (uint16_t)(0x1234u * (k + 1) + 0x111u * i);
      memcpy(e->region + tex + 8 + 2 * k, &v, 2);
    }
    for (k = 0; k < 7; ++k) dc_m2c_b(e, tex + 26 + k, (uint8_t)(k * 9 + i + 1));
    dc_m2c_f(e, tex + 44, 0.75f + 1.5f * (float)i);  /* uv-dist */
    dc_m2c_u(e, shader + 0, 0x1fu); dc_m2c_u(e, shader + 16, 0x3ffu);
    result = result << 8 |
             ((uint32_t)dc_m2c_call(e, "adgif-shader<-texture-with-update!", dc_m2c_ptr(e, shader), dc_m2c_ptr(e, tex),
                                    0, 0, 0, 0) - dc_m2c_ptr(e, shader));
  }
  return result;  /* each call returns its shader */
}

/* ---- time_of_day: time-of-day-interp-colors-scratch (64 colors, two scratchpad
 * chunks, double-buffered) ---- */
static inline uint32_t dc_m2c_time_of_day(dc_m2c_env *e) {
  enum { SPAD = 0x4000, PALETTE = 0x1004, CONTEXT = 0x2004, OUT = 0x3000 };
  static const uint16_t weight[4] = {16, 24, 8, 16};  /* the four palettes' weights, /64 */
  uint32_t i, k;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  dc_m2c_u(e, PALETTE + 4, 64);
  for (i = 0; i < 3 * 1024; ++i) dc_m2c_b(e, PALETTE + 12 + i, (uint8_t)(i * 37u + 11u));
  for (i = 0; i < 4; ++i)
    for (k = 0; k < 8; ++k) {
      uint16_t v = (uint16_t)(weight[i] + (k & 1));
      memcpy(e->region + CONTEXT + 1852 + 16 * i + 2 * k, &v, 2);
    }
  dc_m2c_call(e, "time-of-day-interp-colors-scratch", dc_m2c_ptr(e, OUT), dc_m2c_ptr(e, PALETTE), dc_m2c_ptr(e, CONTEXT),
              0, 0, 0);
  return 0;
}

/* ---- draw_string: draw-string (font12 left-aligned, then font24 with other flags),
 * with color, offset and newline codes ---- */
static inline uint32_t dc_m2c_draw_string(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, VIDEO = 0x0400, WORK = 0x1000, FONT12 = 0x2000, FONT24 = 0x2800, CONTEXT = 0x3004,
         MATRIX = 0x3100, STRING = 0x3204, RESULT = 0x3300, DMA = 0x3404 };
  static const char *const text[2] = {"Jak & Daxter\n~3Lv 12 ~+4ok", "~yPrecursor ~1orb\r~-2 x 99"};
  static const uint32_t flags[2] = {0, 32 | 16 | 2};
  uint32_t i, pass, state = 0xf0e1d2c3u, result = 0;
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*video-parms*", dc_m2c_ptr(e, VIDEO));
  e->set_symbol(e, "*font-work*", dc_m2c_ptr(e, WORK));
  e->set_symbol(e, "*font12-table*", dc_m2c_ptr(e, FONT12));
  e->set_symbol(e, "*font24-table*", dc_m2c_ptr(e, FONT24));
  dc_m2c_vec(e, CAMERA + 732, 2048.0f, 2048.0f, 16777000.0f, 1.0f);
  dc_m2c_vec(e, VIDEO + 64, 1.0f, 1.0f, 1.0f, 1.0f);
  /* positive, nonzero font-work and glyph data: no zero divisors */
  for (i = 0; i < 3008 / 4; ++i) dc_m2c_f(e, WORK + 4 * i, 1.25f + dc_m2c_rand(&state) / 16.0f);
  for (i = 0; i < 0x1000 / 4; ++i) dc_m2c_f(e, FONT12 + 4 * i, 1.5f + dc_m2c_rand(&state) / 16.0f);  /* both tables */
  dc_m2c_vec(e, MATRIX + 0, 1.0f, 0.0f, 0.0f, 0.0f); dc_m2c_vec(e, MATRIX + 16, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, MATRIX + 32, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, MATRIX + 48, 1792.0f, 1840.0f, 0.0f, 1.0f);
  dc_m2c_u(e, CONTEXT + 72, dc_m2c_ptr(e, MATRIX));
  dc_m2c_vec(e, CONTEXT + 12, 10.0f, 20.0f, 1.0f, 1.0f);
  dc_m2c_vec(e, CONTEXT + 44, 600.0f, 400.0f, 1.0f, 1.0f);  /* the box glyphs are drawn in */
  dc_m2c_u(e, CONTEXT + 60, 3);  /* color */
  dc_m2c_u(e, DMA + 4, dc_m2c_ptr(e, DMA + 12));
  for (pass = 0; pass < 2; ++pass) {
    uint32_t before, after, n = (uint32_t)strlen(text[pass]);
    uint64_t width;
    memcpy(e->region + STRING + 4, text[pass], n + 1);
    dc_m2c_u(e, STRING, n);
    dc_m2c_u(e, CONTEXT + 68, flags[pass]);
    memcpy(&before, e->region + DMA + 4, 4);
    width = dc_m2c_call(e, "draw-string", dc_m2c_ptr(e, STRING), dc_m2c_ptr(e, DMA), dc_m2c_ptr(e, CONTEXT), 0, 0, 0);
    memcpy(&after, e->region + DMA + 4, 4);
    dc_m2c_u(e, RESULT + 4 * pass, (uint32_t)width);
    result = result << 16 | ((after - before) & 0xffffu);
  }
  return result;  /* packet bytes per string */
}

typedef struct { const char *name; uint32_t (*run)(dc_m2c_env *); } dc_m2c_case;
static const dc_m2c_case dc_m2c_cases[] = {
  {"sky_tng", dc_m2c_sky},
  {"ripple", dc_m2c_ripple},
  {"collide_mesh", dc_m2c_collide_mesh},
  {"collide_edge_grab", dc_m2c_edge_grab},
  {"generic_effect", dc_m2c_generic_light},
  {"generic_merc", dc_m2c_high_speed_reject},
  {"bones", dc_m2c_bones},
  {"generic_effect2", dc_m2c_generic_no_light},
  {"ocean", dc_m2c_ocean},
  {"collide_probe", dc_m2c_probe_node},
  {"generic_tie", dc_m2c_tie_dma},
  {"joint", dc_m2c_joint},
  {"merc_blend_shape", dc_m2c_blerc_chains},
  {"ocean_vu0", dc_m2c_ocean_wave},
  {"shadow", dc_m2c_shadow},
  {"tfrag", dc_m2c_tfrag_stats},
  {"collide_func", dc_m2c_sphere_triangle},
  {"texture", dc_m2c_adgif_texture},
  {"time_of_day", dc_m2c_time_of_day},
  {"draw_string", dc_m2c_draw_string},
};
#define DC_M2C_CASE_COUNT (sizeof dc_m2c_cases / sizeof dc_m2c_cases[0])

static inline uint32_t dc_m2c_crc32(const unsigned char *p, uint32_t n) {
  uint32_t crc = 0xffffffffu, i, k;
  for (i = 0; i < n; ++i) {
    crc ^= p[i];
    for (k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

/* Runs case i on a zeroed region; returns the normalized region's CRC-32 and the
 * case's own result (packet bytes, kept triangles, ...). */
static inline uint32_t dc_m2c_run_case(dc_m2c_env *e, unsigned i, uint32_t *result) {
  uint32_t at, w;
  memset(e->region, 0, DC_M2C_REGION);
  *result = dc_m2c_cases[i].run(e);
  for (at = 0; at < DC_M2C_REGION; at += 4) {
    memcpy(&w, e->region + at, 4);
    if (w == e->s7) w = DC_M2C_FALSE_MARK;
    else if (w >= e->base && w - e->base < DC_M2C_REGION) w -= e->base;
    else continue;
    memcpy(e->region + at, &w, 4);
  }
  return dc_m2c_crc32(e->region, DC_M2C_REGION);
}
#endif
