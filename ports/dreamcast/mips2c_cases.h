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
 * are called. */
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
