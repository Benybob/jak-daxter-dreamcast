/* mips2c differential cases, shared by the host oracle and the Flycast boot
 * (AI-assisted), ISC.
 *
 * Each case fills one zeroed region at a caller-chosen GOAL address, points the
 * symbols it needs into it, and calls mips2c functions by name through the
 * generated wrappers (float arguments as bits). The host oracle
 * (host_mips2c_oracle.cpp) runs the unchanged C++ the way desktop does; the boot
 * runs the native SH-4 build. Afterwards every aligned word that is a pointer into
 * the region becomes its offset, and #f and #t become markers, so both sides hash
 * the same bytes. Only functions whose paths stay in C++ (no jalr back into GOAL)
 * are called. The region is 64 KiB aligned on both sides, as the port's fake
 * scratchpad is, so SPAD offsets that are multiples of 0x4000 survive ori address
 * forming, and a scratchpad at offset 0 reads as an empty DMA tag. */
#ifndef DC_MIPS2C_CASES_H
#define DC_MIPS2C_CASES_H
#include <stdint.h>
#include <string.h>

#define DC_M2C_REGION 0x10000u
#define DC_M2C_FALSE_MARK 0xfffffff7u
#define DC_M2C_TRUE_MARK 0xffffffffu  /* #t: s7 + 8 */
#define DC_M2C_GOAL_MARK 0xfff0f000u  /* + slot: a stand-in's address */

struct dc_m2c_env;
/* A case's stand-in for a GOAL function the translated code calls with jalr: the
 * eight argument registers in, v0 out. */
typedef uint64_t (*dc_m2c_goal_fn)(struct dc_m2c_env *, const uint64_t *args8);
#define DC_M2C_GOAL_SLOTS 12u

typedef struct dc_m2c_env {
  unsigned char *region; /* DC_M2C_REGION bytes */
  uint32_t base;         /* the region's GOAL address */
  uint32_t s7;           /* #f */
  void (*set_symbol)(struct dc_m2c_env *, const char *name, uint32_t value);
  uint64_t (*call)(struct dc_m2c_env *, const char *name, const uint64_t *args8);
  /* The same call made the way translated code calls another mips2c function:
   * jalr on the GOAL stub (SH-4), desktop's trampoline (host). */
  uint64_t (*call_jalr)(struct dc_m2c_env *, const char *name, const uint64_t *args8);
  /* An address to store in a function symbol: jalr on it runs fn (slot <
   * DC_M2C_GOAL_SLOTS). SH-4: a C thunk; host: dispatched in the jalr trampoline. */
  uint32_t (*goal_function)(struct dc_m2c_env *, unsigned slot, dc_m2c_goal_fn fn);
  /* The address jalr calls for a built mips2c function: its dc-mips2c stub (SH-4),
   * an entry desktop's trampoline dispatches (host). */
  uint32_t (*mips2c_function)(struct dc_m2c_env *, const char *name);
  /* Memory at a GOAL address outside the region (the translated code's stack). */
  void *(*memory)(struct dc_m2c_env *, uint32_t address);
  /* A symbol's address, which load_symbol_addr gives translated code (ntsc, pal). */
  uint32_t (*symbol)(struct dc_m2c_env *, const char *name);
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
static inline void dc_m2c_sky_camera(dc_m2c_env *e, uint32_t camera) {
  dc_m2c_vec(e, camera + 572, 1.0f, 0.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, camera + 588, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, camera + 604, 0.0f, 0.0f, 1.0f, 0.25f);
  dc_m2c_vec(e, camera + 620, 0.0f, 0.0f, -8.0f, 1.0f);
  dc_m2c_vec(e, camera + 700, 256.0f, 224.0f, 1.0f, 1.0f);
  dc_m2c_vec(e, camera + 732, 2048.0f, 2048.0f, 16777000.0f, 1.0f);
  dc_m2c_f(e, camera + 124, 0.5f); dc_m2c_f(e, camera + 128, 0.75f);
  dc_m2c_f(e, camera + 828, 3.0f); dc_m2c_f(e, camera + 848, 1.0f);
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, camera));
}
static inline uint32_t dc_m2c_sky(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, DATA = 0x0404, VF27 = 0x0600, VERTS = 0x0700, DMA = 0x0804, SPAD = 0x4000 };
  uint32_t base_after;
  dc_m2c_sky_camera(e, CAMERA);
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
 * create-wave-table calls GOAL's sin/cos/atan and has its own case, below. */
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
 * The two prepare-dma functions have their own case, below. */
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

/* ---- bones: draw-bones-check-longest-edge-asm (a float argument), a sphere at
 * seven positions and four radii. bones-mtx-calc has its own case, below. */
static inline uint32_t dc_m2c_bones(dc_m2c_env *e) {
  enum { SPAD = 0x4000, CAMERA = 0x0104, DRAW = 0x0404 };
  static const float radius[4] = {0.5f, 4.0f, 40.0f, 400.0f};
  uint32_t i, k, result = 0;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  for (i = 0; i < 32; ++i) dc_m2c_f(e, CAMERA + 4 * i, 1.0f + 0.5f * (float)i);  /* fov, clip planes, ... */
  dc_m2c_f(e, DRAW + 64, 12.0f); dc_m2c_f(e, DRAW + 136, 3.0f);
  for (k = 0; k < 7; ++k) {
    /* In view, then beyond each side, behind and far, so each plane's test runs. */
    static const float at[7][3] = {{2.0f, -1.5f, 30.0f}, {40.0f, 0.0f, 30.0f}, {-40.0f, 0.0f, 30.0f},
                                   {0.0f, 40.0f, 30.0f}, {0.0f, -40.0f, 30.0f}, {0.0f, 0.0f, -30.0f},
                                   {0.0f, 0.0f, 500.0f}};
    dc_m2c_f(e, SPAD + 176, at[k][0]); dc_m2c_f(e, SPAD + 180, at[k][1]); dc_m2c_f(e, SPAD + 184, at[k][2]);
    for (i = 0; i < 4; ++i) {
      uint64_t r = dc_m2c_call(e, "draw-bones-check-longest-edge-asm", dc_m2c_ptr(e, DRAW),
                               dc_m2c_fbits(radius[i]), 0, 0, 0, 0);
      result = result << 1 | (uint32_t)((uint32_t)r != e->s7);
    }
  }
  return result;  /* one bit per position and radius: the answer was not #f */
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
  static const float corner[3][4][3] = {
    {{-1.0f, -1.0f, 4.0f}, {1.0f, -1.0f, 4.0f}, {1.0f, 1.0f, 5.0f}, {-1.0f, 1.0f, 5.0f}},
    /* two corners behind the near plane, so both hyperplane clippers cut */
    {{-2.0f, -1.0f, 6.0f}, {3.0f, -1.0f, -3.0f}, {2.0f, 2.0f, -2.0f}, {-1.0f, 1.5f, 3.0f}},
    /* well inside every plane (x and y are scaled by 256 and 224): nothing is cut */
    {{-0.002f, -0.002f, 4.0f}, {0.002f, -0.002f, 4.0f}, {0.002f, 0.002f, 5.0f}, {-0.002f, 0.002f, 5.0f}}};
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
  for (q = 0; q < 3; ++q) {
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
    if (q == 2) dc_m2c_u(e, QUAD + 0x2f0, after - before);  /* the uncut quad's packet bytes */
    else result = result << 12 | ((after - before) & 0xfffu);
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
  uint32_t i, k, state = 0x7f4a7c15u, result = 0;
  for (i = 0; i < 31 * 4; ++i) dc_m2c_f(e, REGS + 4 * i, dc_m2c_rand(&state));
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
  }
  /* The LOD distances at +80, near to far, so each distance band is taken. */
  for (k = 0; k < 4; ++k) {
    static const float lod[4] = {1.0f, -40.0f, 40.0f, -400.0f};
    dc_m2c_vec(e, WORK + 80, lod[k], lod[k] * 2.0f, lod[k] * 3.0f, lod[k] * 4.0f);
    for (i = 0; i < 8; ++i) dc_m2c_call(e, "stats-tfrag-asm", dc_m2c_ptr(e, FRAGS + 64 * i), 0, 0, 0, 0, 0);
  }
  for (i = 0; i < 2; ++i) {
    uint16_t n;
    memcpy(&n, e->region + STATS + 32 * i + 2, 2);
    result = result << 8 | n;
  }
  return result;  /* fragments counted in each stats record */
}

/* ---- collide_func: moving-sphere-triangle-intersect, called through jalr on its
 * stub (float argument and result as GPR bits). Face hits only; the
 * collide-do-primitives case below takes the edge path. ---- */
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

/* ---- draw_string: draw-string with four flag sets (left; large, right and
 * kerning; shadow and middle; shadow, middle-vert and large), with color, offset,
 * signed and multi-digit codes and newlines ---- */
static inline uint32_t dc_m2c_draw_string(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, VIDEO = 0x0400, WORK = 0x1000, FONT12 = 0x2000, FONT24 = 0x2800, CONTEXT = 0x3004,
         MATRIX = 0x3100, STRING = 0x3204, RESULT = 0x3300, DMA = 0x3404 };
  static const char *const text[4] = {"Jak & Daxter\n~3Lv 12 ~+4ok", "~yPrecursor ~1orb\r~-2 x 99",
                                       "~12L~+10H~-3Vmid ~0k~1K\nsh~Ydow ~[~]", "~h~v~25Y~-18V Big\n~2ll ~33xx"};
  /* left; large|right|kerning; shadow|middle; shadow|middle-vert|large */
  static const uint32_t flags[4] = {0, 32 | 16 | 2, 1 | 4, 1 | 8 | 32};
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
  for (pass = 0; pass < 4; ++pass) {
    uint32_t before, after, n = (uint32_t)strlen(text[pass]);
    uint64_t width;
    memcpy(e->region + STRING + 4, text[pass], n + 1);
    dc_m2c_u(e, STRING, n);
    dc_m2c_u(e, CONTEXT + 68, flags[pass]);
    memcpy(&before, e->region + DMA + 4, 4);
    width = dc_m2c_call(e, "draw-string", dc_m2c_ptr(e, STRING), dc_m2c_ptr(e, DMA), dc_m2c_ptr(e, CONTEXT), 0, 0, 0);
    memcpy(&after, e->region + DMA + 4, 4);
    dc_m2c_u(e, RESULT + 4 * pass, (uint32_t)width);
    dc_m2c_u(e, RESULT + 16 + 4 * pass, after - before);
    result = result * 31 + (after - before);
  }
  return result;  /* a hash of each string's packet bytes (also at RESULT + 16) */
}

/* ---- GOAL stand-ins (env->goal_function) for the cases below. They log their calls
 * in the region and only move bits: no float arithmetic, which C compilers may
 * round differently on the two sides. ---- */
#define DC_M2C_LOG 0x0e00u  /* [0] count, then one word per entry */
static inline void dc_m2c_log(dc_m2c_env *e, uint32_t value) {
  uint32_t n;
  memcpy(&n, e->region + DC_M2C_LOG, 4);
  if (n < 127) { dc_m2c_u(e, DC_M2C_LOG + 4 + 4 * n, value); dc_m2c_u(e, DC_M2C_LOG, n + 1); }
}
static inline uint32_t dc_m2c_off(dc_m2c_env *e, uint64_t address) { return (uint32_t)address - e->base; }
/* The stand-in addresses this case handed out: they differ between the two sides,
 * so the normalization turns any copy of them in the region into a marker. */
static uint32_t dc_m2c_goal_address[DC_M2C_GOAL_SLOTS];
static inline uint32_t dc_m2c_goal(dc_m2c_env *e, unsigned slot, dc_m2c_goal_fn fn) {
  return dc_m2c_goal_address[slot] = e->goal_function(e, slot, fn);
}

/* ---- shadow: shadow-execute over one shadow object (a tetrahedron), with
 * flush-cache and camera-pos standing in for GOAL ---- */
enum { DC_M2C_SHADOW_CAMPOS = 0x0400 };
static uint64_t dc_m2c_flush_cache(dc_m2c_env *e, const uint64_t *a) { dc_m2c_log(e, 0xf1u << 24 | (uint32_t)a[0]); return 0; }
static uint64_t dc_m2c_camera_pos(dc_m2c_env *e, const uint64_t *a) {
  (void)a; dc_m2c_log(e, 0xca << 24); return dc_m2c_ptr(e, DC_M2C_SHADOW_CAMPOS);
}
static inline uint32_t dc_m2c_shadow_execute(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, SDATA = 0x0600, CHAIN = 0x1000, GSF = 0x8000, OUT = 0xa000, SPAD = 0x4000 };
  enum { SETTINGS = CHAIN + 16, GEO = CHAIN + 112, GEO_BYTES = 528 };
  static const float vert[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 0, 2}, {0, 2, 0}};
  static const uint8_t tris[16] = {0, 1, 2, 0, 0, 3, 1, 0, 1, 3, 2, 0, 2, 3, 0, 0};
  static const uint8_t edges[24] = {0, 1, 0, 1, 1, 2, 0, 2, 2, 0, 0, 3, 0, 3, 1, 3, 3, 1, 1, 2, 3, 2, 2, 3};
  static const uint16_t counts[6] = {4, 0, 4, 6, 0, 0};       /* verts, double verts, tris, edges, double tris/edges */
  static const uint32_t offsets[6] = {400, 464, 480, 496, 528, 528};
  uint32_t i, state = 0x5ade5u;
  uint64_t end;
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*gsf-buffer*", dc_m2c_ptr(e, GSF));
  e->set_symbol(e, "flush-cache", dc_m2c_goal(e, 0, dc_m2c_flush_cache));
  e->set_symbol(e, "camera-pos", dc_m2c_goal(e, 1, dc_m2c_camera_pos));
  e->set_symbol(e, "*shadow-data*", dc_m2c_ptr(e, SDATA));
  for (i = 0; i < 64; ++i) dc_m2c_u(e, SDATA + 4 * i, 0x10000000u * (i & 7) + 0x101u * i);  /* gif tags */
  dc_m2c_f(e, CAMERA + 0, 1.0f);
  for (i = 0; i < 16; ++i) dc_m2c_f(e, CAMERA + 364 + 4 * i, (i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.0625f);
  dc_m2c_vec(e, DC_M2C_SHADOW_CAMPOS, 0.0f, 10.0f, 20.0f, 1.0f);
  /* the chain: one cnt tag over the settings and the geometry, then end */
  dc_m2c_u(e, CHAIN + 0, 1u << 28 | (96 + GEO_BYTES) / 16);
  dc_m2c_u(e, CHAIN + 16 + 96 + GEO_BYTES, 7u << 28);
  dc_m2c_vec(e, SETTINGS + 0, 0.5f, 3.0f, 0.5f, 0.0f); dc_m2c_u(e, SETTINGS + 12, 1 | 8);  /* camera check, scissor top */
  dc_m2c_vec(e, SETTINGS + 16, 0.25f, -1.0f, 0.125f, 8.0f);
  dc_m2c_vec(e, SETTINGS + 32, 0.0f, 1.0f, 0.0f, -0.5f);
  dc_m2c_vec(e, SETTINGS + 48, 0.0f, 1.0f, 0.0f, 4.0f);
  dc_m2c_vec(e, SETTINGS + 64, 0.0f, -1.0f, 0.0f, 6.0f);
  for (i = 0; i < 6; ++i) {
    memcpy(e->region + GEO + 8 + 2 * i, &counts[i], 2);
    dc_m2c_u(e, GEO + 20 + 4 * i, offsets[i]);
  }
  for (i = 0; i < 2 * 32; ++i)  /* two joints' 128-byte records: a matrix first */
    dc_m2c_f(e, GEO + 144 + 4 * i, (i % 32 < 16) ? ((i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.125f) : 0.0f);
  for (i = 0; i < 4; ++i) {
    dc_m2c_vec(e, GEO + 400 + 16 * i, vert[i][0], vert[i][1], vert[i][2], 1.0f);
    dc_m2c_b(e, GEO + 464 + 2 * i, (uint8_t)(i & 1));
  }
  memcpy(e->region + GEO + 480, tris, sizeof tris);
  memcpy(e->region + GEO + 496, edges, sizeof edges);
  end = dc_m2c_call(e, "shadow-execute", dc_m2c_ptr(e, CHAIN), dc_m2c_ptr(e, OUT), 0, 0, 0, 0);
  return dc_m2c_off(e, end) - OUT;  /* packet bytes */
}

/* ---- sparticle: sp-process-block-2d and -3d over six particles each, with the
 * particle callback, sp-relaunch-particle-2d/3d, sp-orbiter, sp-free-particle and
 * quaternion*! standing in for GOAL ---- */
static uint64_t dc_m2c_sp_free(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0xfe << 24 | (uint32_t)a[1]);
  dc_m2c_u(e, dc_m2c_off(e, a[2]) + 128, e->s7);  /* the particle is dead */
  return 0;
}
static uint64_t dc_m2c_sp_callback(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0xcb << 24 | (dc_m2c_off(e, a[1]) & 0xffffu));
  dc_m2c_u(e, dc_m2c_off(e, a[1]) + 124, 0xc0ffee00u | (uint32_t)(a[2] & 0xff));
  return 0;
}
static uint64_t dc_m2c_sp_relaunch(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x7e << 24 | (dc_m2c_off(e, a[2]) & 0xffffu));
  dc_m2c_u(e, dc_m2c_off(e, a[2]) + 116, 10);
  dc_m2c_u(e, dc_m2c_off(e, a[2]) + 120, 0);
  return 0;
}
static uint64_t dc_m2c_sp_orbiter(dc_m2c_env *e, const uint64_t *a) {
  uint32_t sprite = dc_m2c_off(e, a[2]), x;
  dc_m2c_log(e, 0x0b << 24 | (sprite & 0xffffu));
  memcpy(&x, e->region + sprite, 4); dc_m2c_u(e, sprite, x ^ 0x00800000u);  /* x doubled or halved: an exponent bit */
  return 0;
}
/* quaternion*!: a0 <- a permutation of a2 (a0 is the caller's stack quaternion). */
static uint64_t dc_m2c_quaternion_mul(dc_m2c_env *e, const uint64_t *a) {
  uint32_t q[4], r[4];
  memcpy(q, e->memory(e, (uint32_t)a[2]), 16);
  r[0] = q[1]; r[1] = q[2]; r[2] = q[0]; r[3] = q[3] ^ 0x80000000u;
  memcpy(e->memory(e, (uint32_t)a[0]), r, 16);
  dc_m2c_log(e, 0x9a << 24 | (dc_m2c_off(e, a[2]) & 0xffffu));
  return a[0];
}
static inline uint32_t dc_m2c_sparticle(dc_m2c_env *e) {
  enum { TIME = 0x0300, SYS = 0x0404, RELAUNCH = 0x0500, CPU = 0x1000, SPRITES = 0x2000 };
  /* timer, flags, callback?, relaunch timer (with a relaunch), alive */
  static const int32_t timer[6] = {-1, 5, 0, -1, 7, 3};
  static const uint32_t flags[6] = {0, 64, 0, 2 | 4, 128 | 1, 0};
  static const uint8_t callback[6] = {0, 1, 0, 0, 1, 0}, relaunch[6] = {0, 0, 0, 1, 0, 0}, alive[6] = {1, 1, 1, 1, 1, 0};
  uint32_t pass, i, k, state = 0x51a2c3u, result = 0;
  uint32_t free_fn = dc_m2c_goal(e, 0, dc_m2c_sp_free), callback_fn = dc_m2c_goal(e, 1, dc_m2c_sp_callback);
  e->set_symbol(e, "*sp-frame-time*", dc_m2c_ptr(e, TIME));
  e->set_symbol(e, "sp-free-particle", free_fn);
  e->set_symbol(e, "sp-orbiter", dc_m2c_goal(e, 2, dc_m2c_sp_orbiter));
  e->set_symbol(e, "sp-relaunch-particle-2d", dc_m2c_goal(e, 3, dc_m2c_sp_relaunch));
  e->set_symbol(e, "sp-relaunch-particle-3d", dc_m2c_goal(e, 4, dc_m2c_sp_relaunch));
  e->set_symbol(e, "quaternion*!", dc_m2c_goal(e, 5, dc_m2c_quaternion_mul));
  dc_m2c_u(e, TIME, 2);  /* frames this tick */
  for (pass = 0; pass < 4; ++pass) {  /* 2d running, 2d paused, 3d running, 3d paused */
    uint32_t cpu = CPU + 0x1000 * pass, sprites = SPRITES + 0x1000 * pass;
    uint64_t r;
    for (i = 0; i < 6; ++i) {
      uint32_t p = cpu + 144 * i, s = sprites + 48 * i;
      for (k = 0; k < 6; ++k) dc_m2c_rand_vec(e, p + 16 + 16 * k, &state, dc_m2c_rand(&state) / 8.0f);
      dc_m2c_f(e, p + 96, i & 1 ? 0.0f : 0.875f);  /* friction, or none */
      dc_m2c_u(e, p + 100, (uint32_t)timer[i]);
      dc_m2c_u(e, p + 104, flags[i] | (pass % 2 == 1 && i == 0 ? 8192u : 0u));
      dc_m2c_u(e, p + 112, callback[i] ? callback_fn : 0);
      dc_m2c_u(e, p + 116, relaunch[i] ? (pass >= 2 ? 1 : 2) : 0);  /* 3d: runs out this tick */
      dc_m2c_u(e, p + 120, relaunch[i] ? dc_m2c_ptr(e, RELAUNCH) : 0);
      dc_m2c_u(e, p + 124, 0x40000000u + (i << 16));  /* a normal float: SH-4 flushes denormals, x86 does not */
      dc_m2c_u(e, p + 128, alive[i] ? dc_m2c_ptr(e, SYS) : e->s7);
      for (k = 0; k < 3; ++k) dc_m2c_rand_vec(e, s + 16 * k, &state, 1.0f + (float)k);
    }
    {
      const uint64_t args[8] = {dc_m2c_ptr(e, SYS), dc_m2c_ptr(e, cpu), dc_m2c_ptr(e, sprites), 0, 6,
                                pass % 2 == 1 ? (uint64_t)e->s7 + 8 : e->s7, 0, 0};
      r = e->call(e, pass >= 2 ? "sp-process-block-3d" : "sp-process-block-2d", args);
    }
    result = result << 8 | (uint32_t)(r & 0xff);
  }
  return result;  /* particle indices reached per pass */
}

/* ---- sparticle_launcher: particle-adgif's cache (miss, last hit, search miss,
 * search hit), particle-setup-adgif standing in for GOAL ---- */
static uint64_t dc_m2c_setup_adgif(dc_m2c_env *e, const uint64_t *a) {
  uint32_t entry = dc_m2c_off(e, a[0]), i;
  dc_m2c_log(e, 0xad << 24 | (entry & 0xffffu));
  for (i = 0; i < 20; ++i) dc_m2c_u(e, entry + 4 * i, (uint32_t)a[1] * 3u + i);
  return 0;
}
static inline uint32_t dc_m2c_particle_adgif(dc_m2c_env *e) {
  enum { CACHE = 0x1004, OUT = 0x4000 };
  static const uint32_t texture[6] = {0x00123400u, 0x00123400u, 0x02a05600u, 0x00123400u, 0x02a05600u, 0x07707700u};
  uint32_t i, count;
  e->set_symbol(e, "*particle-adgif-cache*", dc_m2c_ptr(e, CACHE));
  e->set_symbol(e, "particle-setup-adgif", dc_m2c_goal(e, 0, dc_m2c_setup_adgif));
  for (i = 0; i < 6; ++i)
    dc_m2c_call(e, "particle-adgif", dc_m2c_ptr(e, OUT + 0x80 * i), texture[i], 0, 0, 0, 0);
  memcpy(&count, e->region + CACHE, 4);
  return count;  /* cache entries made */
}

/* ---- sparticle_launcher: sp-launch-particles-var, one 2d launcher spawning three
 * particles. sp-init-fields! is modelled exactly (as sparticle-launcher.gc has it,
 * with every random range 0); sp-get-particle hands out a pool's particles;
 * particle-adgif is the built mips2c function, reached through jalr. ---- */
enum { DC_M2C_LAUNCH_POOL = 0x2000, DC_M2C_LAUNCH_POOL_SIZE = 4 };
static uint64_t dc_m2c_sp_init_fields(dc_m2c_env *e, const uint64_t *a) {
  uint32_t out = (uint32_t)a[0], spec = (uint32_t)a[1], field = (uint32_t)a[2] + 1, end = (uint32_t)a[3];
  int missing = (uint32_t)a[4] != e->s7;
  uint16_t id, flags;
  uint32_t value;
  dc_m2c_log(e, 0x1f << 24 | field << 8 | end);
  for (;;) {
    memcpy(&id, e->region + dc_m2c_off(e, spec), 2);
    if (id >= field) break;
    spec += 16;
  }
  if (field >= end) return spec;
  while (field != end) {
    memcpy(&id, e->region + dc_m2c_off(e, spec), 2);
    memcpy(&flags, e->region + dc_m2c_off(e, spec) + 2, 2);
    memcpy(&value, e->region + dc_m2c_off(e, spec) + 4, 4);
    if (id != field) {
      if (missing) memset(e->memory(e, out), 0, 4);
      spec -= 16;
    } else if (flags == 3) {  /* copy-from-other: the word value words away */
      memcpy(e->memory(e, out), e->memory(e, out + 4 * (int32_t)value), 4);
    } else {  /* int, float, object (random range 0): the value itself */
      memcpy(e->memory(e, out), &value, 4);
    }
    ++field; out += 4; spec += 16;
  }
  return spec;
}
static uint64_t dc_m2c_sp_get_particle(dc_m2c_env *e, const uint64_t *a) {
  uint32_t used, cpu;
  (void)a;
  memcpy(&used, e->region + DC_M2C_LAUNCH_POOL, 4);
  dc_m2c_log(e, 0x9e << 24 | used);
  if (used == DC_M2C_LAUNCH_POOL_SIZE) return e->s7;
  dc_m2c_u(e, DC_M2C_LAUNCH_POOL, used + 1);
  cpu = DC_M2C_LAUNCH_POOL + 0x10 + 144 * used;
  dc_m2c_u(e, cpu + 128, e->s7 + 8);  /* valid */
  return dc_m2c_ptr(e, cpu);
}
static uint64_t dc_m2c_sp_adjust_launch(dc_m2c_env *e, const uint64_t *a) {
  uint32_t value;
  memcpy(&value, e->region + dc_m2c_off(e, a[2]) + 4, 4);
  dc_m2c_log(e, 0xa1 << 24 | (dc_m2c_off(e, a[1]) & 0xffffu));
  dc_m2c_u(e, dc_m2c_off(e, a[1]) + 16, value);
  return 0;
}
static uint64_t dc_m2c_sp_birth(dc_m2c_env *e, const uint64_t *a) {
  uint32_t x;
  memcpy(&x, e->memory(e, (uint32_t)a[2]), 4);  /* the launch info on the launcher's stack */
  dc_m2c_log(e, 0xb1 << 24 | (dc_m2c_off(e, a[1]) & 0xffffu));
  dc_m2c_log(e, x);
  return 0;
}
static inline uint32_t dc_m2c_launch_particles(dc_m2c_env *e) {
  enum { SYSTEM = 0x0104, LAUNCHER = 0x0204, POS = 0x0300, SPECS = 0x0400, LEVEL = 0x0800, TOD = 0x1804,
         CACHE = 0x3004, SPRITES = 0x5000, ADGIFS = 0x5400 };
  static const struct { uint16_t field, flags; uint32_t value; } spec[] = {
    {1, 0, 0x00123400u},   /* spt-texture */
    {4, 4, 0},             /* spt-birth-func: the stand-in, below */
    {6, 1, 0x40400000u},   /* spt-num 3.0 */
    {10, 1, 0x3fc00000u}, {11, 1, 0xc0000000u}, {12, 1, 0x3f000000u},  /* x, y, z */
    {13, 1, 0x3e800000u},  /* scale-x */
    {17, 3, (uint32_t)-4}, /* scale-y: copy scale-x */
    {18, 1, 0x43000000u}, {21, 1, 0x42800000u},  /* r 128, a 64 */
    {25, 1, 0x3f000000u},  /* vel-y */
    {46, 0, 300},          /* timer */
    {47, 0, 4096 | 8},     /* flags: time-of-day colors, adgif 72 */
    {54, 1, 0x3dcccccdu},  /* launchrot-x: sp-adjust-launch runs */
    {70, 0, 0}};
  uint32_t i, used;
  e->set_symbol(e, "*sp-launcher-enable*", e->s7 + 8);
  e->set_symbol(e, "*sp-launcher-lock*", e->s7);
  e->set_symbol(e, "*level*", dc_m2c_ptr(e, LEVEL));
  e->set_symbol(e, "*time-of-day-context*", dc_m2c_ptr(e, TOD));
  e->set_symbol(e, "*particle-adgif-cache*", dc_m2c_ptr(e, CACHE));
  e->set_symbol(e, "sp-init-fields!", dc_m2c_goal(e, 0, dc_m2c_sp_init_fields));
  e->set_symbol(e, "sp-get-particle", dc_m2c_goal(e, 1, dc_m2c_sp_get_particle));
  e->set_symbol(e, "sp-adjust-launch", dc_m2c_goal(e, 2, dc_m2c_sp_adjust_launch));
  e->set_symbol(e, "particle-setup-adgif", dc_m2c_goal(e, 3, dc_m2c_setup_adgif));
  e->set_symbol(e, "particle-adgif", e->mips2c_function(e, "particle-adgif"));
  for (i = 0; i < sizeof spec / sizeof spec[0]; ++i) {
    memcpy(e->region + SPECS + 16 * i, &spec[i].field, 2);
    memcpy(e->region + SPECS + 16 * i + 2, &spec[i].flags, 2);
    dc_m2c_u(e, SPECS + 16 * i + 4, spec[i].field == 4 ? dc_m2c_goal(e, 4, dc_m2c_sp_birth) : spec[i].value);
  }
  dc_m2c_u(e, SYSTEM + 24, e->s7);  /* 2d */
  dc_m2c_u(e, LAUNCHER + 8, dc_m2c_ptr(e, SPECS));
  dc_m2c_vec(e, POS, 10.0f, 20.0f, 30.0f, 1.0f);
  dc_m2c_u(e, LEVEL + 124, dc_m2c_ptr(e, 0)); dc_m2c_u(e, LEVEL + 128, dc_m2c_ptr(e, 0x1000));  /* level 0's heap */
  dc_m2c_vec(e, TOD + 108, 1.0f, 0.5f, 0.25f, 1.0f);
  for (i = 0; i < DC_M2C_LAUNCH_POOL_SIZE; ++i) {
    uint32_t cpu = DC_M2C_LAUNCH_POOL + 0x10 + 144 * i;
    dc_m2c_u(e, cpu + 0, dc_m2c_ptr(e, SPRITES + 48 * i));
    dc_m2c_u(e, cpu + 4, dc_m2c_ptr(e, ADGIFS + 0x60 * i));
  }
  {
    const uint64_t args[8] = {dc_m2c_ptr(e, SYSTEM), dc_m2c_ptr(e, LAUNCHER), dc_m2c_ptr(e, POS), e->s7, e->s7,
                              dc_m2c_fbits(1.0f), 0, 0};
    e->call(e, "sp-launch-particles-var", args);
  }
  memcpy(&used, e->region + DC_M2C_LAUNCH_POOL, 4);
  return used;  /* particles launched */
}

/* ---- ripple: ripple-create-wave-table over two waveforms, with sin, cos and atan
 * standing in for GOAL (fixed results, so no float math happens outside the
 * translated code). One waveform is ntsc, one pal; a third call finds its
 * waveform already converted and only rebuilds the table. ---- */
static uint64_t dc_m2c_ripple_atan(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, (uint32_t)a[0]); dc_m2c_log(e, (uint32_t)a[1]);
  return 0x46800000u;  /* 16384.0: an eighth of a turn */
}
static uint64_t dc_m2c_ripple_cos(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0xc0 << 24 | ((uint32_t)a[0] >> 8));
  return 0x3f400000u;  /* 0.75 */
}
static uint64_t dc_m2c_ripple_sin(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x51 << 24 | ((uint32_t)a[0] >> 8));
  return 0xbf200000u;  /* -0.625 */
}
static uint64_t dc_m2c_ripple_offs(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x0f << 24 | dc_m2c_off(e, a[0]));
  return 0;
}
static inline void dc_m2c_ripple_wave(dc_m2c_env *e, uint32_t at, float scale, float offset, int16_t xdiv,
                                      int16_t zdiv, float speed) {
  dc_m2c_f(e, at + 0, scale); dc_m2c_f(e, at + 4, offset);
  memcpy(e->region + at + 8, &xdiv, 2); memcpy(e->region + at + 10, &zdiv, 2);
  dc_m2c_f(e, at + 12, speed);
}
static inline uint32_t dc_m2c_ripple_wave_table(dc_m2c_env *e) {
  enum { POLY = 0x0010, SETTINGS = 0x0104, NTSC = 0x0200, PAL = 0x0300, SPAD = 0x4000 };
  uint32_t i, sum = 0;
  dc_m2c_vec(e, POLY, -0.49999788f, 0.041664604f, -0.0013868269f, 0.000023717716f);
  e->set_symbol(e, "*cos-poly-vec*", dc_m2c_ptr(e, POLY));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*setting-control*", dc_m2c_ptr(e, SETTINGS));
  e->set_symbol(e, "atan", dc_m2c_goal(e, 0, dc_m2c_ripple_atan));
  e->set_symbol(e, "cos", dc_m2c_goal(e, 1, dc_m2c_ripple_cos));
  e->set_symbol(e, "sin", dc_m2c_goal(e, 2, dc_m2c_ripple_sin));
  e->set_symbol(e, "ripple-update-waveform-offs", dc_m2c_goal(e, 3, dc_m2c_ripple_offs));
  dc_m2c_call0(e, "ripple-execute-init");  /* the table create-wave-table samples */
  /* Waveforms: count, converted (#f), falloff, then 28-byte waves. |(3, 4)| = 5 and
   * |(-5, 12)| = 13, so the square roots are exact. */
  dc_m2c_u(e, NTSC + 0, 2); dc_m2c_u(e, NTSC + 4, e->s7); dc_m2c_f(e, NTSC + 12, 0.75f);
  dc_m2c_ripple_wave(e, NTSC + 16, 0.5f, 1024.0f, 3, 4, 2.0f);
  dc_m2c_ripple_wave(e, NTSC + 44, 0.25f, -512.0f, -5, 12, 1.5f);
  dc_m2c_u(e, PAL + 0, 1); dc_m2c_u(e, PAL + 4, e->s7); dc_m2c_f(e, PAL + 12, 1.5f);
  dc_m2c_ripple_wave(e, PAL + 16, 1.25f, 300.0f, 4, 3, -3.0f);
  dc_m2c_u(e, SETTINGS + 80, e->symbol(e, "ntsc"));
  dc_m2c_call(e, "ripple-create-wave-table", dc_m2c_ptr(e, NTSC), 0, 0, 0, 0, 0);
  dc_m2c_u(e, SETTINGS + 80, e->symbol(e, "pal"));
  dc_m2c_call(e, "ripple-create-wave-table", dc_m2c_ptr(e, PAL), 0, 0, 0, 0, 0);
  dc_m2c_call(e, "ripple-create-wave-table", dc_m2c_ptr(e, NTSC), 0, 0, 0, 0, 0);
  dc_m2c_u(e, SETTINGS + 80, 0);  /* the symbol's address differs between the sides */
  for (i = 0; i < 256; ++i) sum += e->region[SPAD + 1024 + i];
  return sum;  /* the byte table's sum */
}

/* ---- bones: bones-mtx-calc over 20 bones, so both scratchpad buffers and the
 * second batch run. The PC port's bones-mtx-calc-execute calls the GOAL
 * new-bones-mtx-calc-asm while *use-new-bones* is #t (its default), and this
 * function when it is #f. The scratchpad layout is the one that
 * bones-mtx-calc-execute sets up. ---- */
static inline uint32_t dc_m2c_bones_mtx_calc(dc_m2c_env *e) {
  enum { CAMERA = 0x0100, JOINTS = 0x1000, BONES = 0x2000, OUT = 0x3000, SPAD = 0x4000, COUNT = 20 };
  uint32_t i, k, state = 0x0badf00du, nonzero = 0, w;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  /* bone-memory's work layout at scratchpad + 16: joint, bone, output buffers. */
  dc_m2c_u(e, SPAD + 16, dc_m2c_ptr(e, SPAD + 256)); dc_m2c_u(e, SPAD + 20, dc_m2c_ptr(e, SPAD + 4864));
  dc_m2c_u(e, SPAD + 24, dc_m2c_ptr(e, SPAD + 1280)); dc_m2c_u(e, SPAD + 28, dc_m2c_ptr(e, SPAD + 5888));
  dc_m2c_u(e, SPAD + 32, dc_m2c_ptr(e, SPAD + 2816)); dc_m2c_u(e, SPAD + 36, dc_m2c_ptr(e, SPAD + 7424));
  /* The camera: a rotation about y and a translation. */
  dc_m2c_vec(e, CAMERA + 0, 0.6f, 0.0f, -0.8f, 0.0f); dc_m2c_vec(e, CAMERA + 16, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, CAMERA + 32, 0.8f, 0.0f, 0.6f, 0.0f); dc_m2c_vec(e, CAMERA + 48, 5.0f, -2.0f, 30.0f, 1.0f);
  /* Joints are 80 bytes, their bind pose 16 bytes in; bones are 96 bytes with
   * their transform first. Affine matrices of random rows. */
  for (i = 0; i < COUNT; ++i) {
    for (k = 0; k < 3; ++k) dc_m2c_rand_vec(e, JOINTS + 80 * i + 16 * k, &state, 0.0f);
    dc_m2c_rand_vec(e, JOINTS + 80 * i + 48, &state, 1.0f);
    dc_m2c_u(e, JOINTS + 80 * i + 64, 0xa0000000u | i);  /* the next joint's header: skipped */
    for (k = 0; k < 3; ++k) dc_m2c_rand_vec(e, BONES + 96 * i + 16 * k, &state, 0.0f);
    dc_m2c_rand_vec(e, BONES + 96 * i + 48, &state, 1.0f);
    dc_m2c_rand_vec(e, BONES + 96 * i + 64, &state, 1.0f);
    dc_m2c_rand_vec(e, BONES + 96 * i + 80, &state, 1.0f);
  }
  {
    /* joints points 68 bytes past the first bind pose (a basic at 16n+4, its
     * matrix 12 bytes in, one joint along). */
    const uint64_t args[8] = {dc_m2c_ptr(e, OUT), dc_m2c_ptr(e, JOINTS + 68), dc_m2c_ptr(e, BONES), COUNT,
                              dc_m2c_ptr(e, CAMERA), 0, 0, 0};
    e->call(e, "bones-mtx-calc", args);
  }
  for (i = 0; i < COUNT * 128; i += 4) {
    memcpy(&w, e->region + OUT + i, 4);
    nonzero += w != 0;
  }
  return nonzero;  /* output words written */
}

/* ---- collide_cache: the queries over a filled cache. puyp-mesh (method 30),
 * resolve-moving-sphere-tri and -sphere (collide-cache-prim methods 9 and 10) and
 * collide-puss-work methods 9 and 10, over seven triangles placed by hand. The GOAL
 * intersection routines are stand-ins: moving-sphere-triangle-intersect returns
 * the u a triangle carries in user32 and reports its first vertex as the contact
 * point, so the translated code's own ordering, direction and best-u logic
 * decides. ---- */
enum { DC_M2C_CC_CACHE = 0x1004, DC_M2C_CC_TRIS = 0x1000 + 4912 };
static uint64_t dc_m2c_cc_sphere_tri(dc_m2c_env *e, const uint64_t *a) {
  static const uint32_t up[4] = {0, 0x3f800000u, 0, 0};
  uint32_t tri = dc_m2c_off(e, a[3]), u;
  dc_m2c_log(e, 0x57 << 24 | tri);
  memcpy(e->memory(e, (uint32_t)a[4]), e->region + tri, 16);  /* contact: the first vertex */
  memcpy(e->memory(e, (uint32_t)a[5]), up, 16);              /* normal: +y */
  memcpy(&u, e->region + tri + 56, 4);
  return u;
}
static uint64_t dc_m2c_cc_sphere_sphere(dc_m2c_env *e, const uint64_t *a) {
  static const float point[4] = {1.5f, 2.0f, -0.5f, 1.0f};
  dc_m2c_log(e, 0x55 << 24 | dc_m2c_off(e, a[2]));
  memcpy(e->memory(e, (uint32_t)a[3]), point, 16);
  return 0x3e99999au;  /* 0.3 */
}
static uint64_t dc_m2c_cc_closest_pt(dc_m2c_env *e, const uint64_t *a) {
  uint32_t tri = dc_m2c_off(e, a[2]);
  dc_m2c_log(e, 0xc1 << 24 | tri);
  memcpy(e->region + dc_m2c_off(e, a[0]), e->region + tri, 16);  /* the first vertex */
  return 0;
}
static inline void dc_m2c_cc_tri(dc_m2c_env *e, unsigned i, const float v[9], uint32_t pat, uint32_t u) {
  uint32_t at = DC_M2C_CC_TRIS + 64 * i, k;
  for (k = 0; k < 3; ++k) dc_m2c_vec(e, at + 16 * k, v[3 * k], v[3 * k + 1], v[3 * k + 2], 1.0f);
  dc_m2c_u(e, at + 48, pat);
  dc_m2c_u(e, at + 52, i << 16);  /* prim-index 0, user16 i */
  dc_m2c_u(e, at + 56, u);
}
static inline void dc_m2c_cc_sphere(dc_m2c_env *e, uint32_t at, float x, float y, float z, float r,
                                    const int32_t box[6]) {
  unsigned k;
  dc_m2c_vec(e, at, x, y, z, r);
  for (k = 0; k < 3; ++k) { dc_m2c_u(e, at + 16 + 4 * k, (uint32_t)box[k]); dc_m2c_u(e, at + 32 + 4 * k, (uint32_t)box[3 + k]); }
}
static inline uint32_t dc_m2c_collide_cache_query(dc_m2c_env *e) {
  enum { PRIM = 0x1000 + 112, FAR = PRIM + 48, SHAPE = 0x0104, PUYP = 0x0200, PCORE = 0x0300, MOVE = 0x0320,
         RESULTS = 0x0340, OUT = 0x0400, WORK = 0x4000, PARAMS = 0x0380 };
  /* Floors wound so their normal is +y: v0, v0 + z, v0 + x. */
#define DC_M2C_CC_UP(y) {-4, y, -4, -4, y, 8, 8, y, -4}
  static const float below[9] = DC_M2C_CC_UP(-3), floor_down[9] = {-4, 0, -4, 8, 0, -4, -4, 0, 8},
                     floor_up[9] = DC_M2C_CC_UP(0), ignored[9] = DC_M2C_CC_UP(1),
                     wall[9] = {2, -4, -4, 2, -4, 8, 2, 8, -4}, small[9] = {4, 2, 4, 4, 2, 7, 7, 2, 4},
                     above[9] = DC_M2C_CC_UP(6);
#undef DC_M2C_CC_UP
  static const int32_t box0[6] = {19, -1, -1, 21, 1, 1}, box1[6] = {-6, -1, -6, -2, 3, -2},
                       box2[6] = {1, 1, 1, 3, 3, 3};
  uint32_t i, mask = 0;
  uint64_t r;
  e->set_symbol(e, "moving-sphere-triangle-intersect", dc_m2c_goal(e, 0, dc_m2c_cc_sphere_tri));
  e->set_symbol(e, "moving-sphere-sphere-intersect", dc_m2c_goal(e, 1, dc_m2c_cc_sphere_sphere));
  e->set_symbol(e, "closest-pt-in-triangle", dc_m2c_goal(e, 2, dc_m2c_cc_closest_pt));
  dc_m2c_cc_tri(e, 0, below, 1, 0x3f4ccccdu);      /* u 0.8 */
  dc_m2c_cc_tri(e, 1, floor_down, 1, 0xbf800000u); /* u -1: behind */
  dc_m2c_cc_tri(e, 2, floor_up, 1, 0x3f000000u);   /* u 0.5 */
  dc_m2c_cc_tri(e, 3, ignored, 9, 0x3e800000u);    /* u 0.25, a pat puyp ignores */
  dc_m2c_cc_tri(e, 4, wall, 1, 0x3ecccccdu);       /* u 0.4 */
  dc_m2c_cc_tri(e, 5, small, 1, 0x40000000u);      /* u 2 */
  dc_m2c_cc_tri(e, 6, above, 1, 0x3e000000u);      /* u 0.125, contact above the sphere */
  dc_m2c_u(e, DC_M2C_CC_CACHE + 0, 7); dc_m2c_u(e, DC_M2C_CC_CACHE + 4, 2);
  /* The prim: a world sphere, the cache, its shape (pat at +68) and tris 0..6. */
  dc_m2c_vec(e, PRIM, 1.0f, 1.0f, 1.0f, 2.0f);
  dc_m2c_u(e, PRIM + 32, dc_m2c_ptr(e, DC_M2C_CC_CACHE)); dc_m2c_u(e, PRIM + 36, dc_m2c_ptr(e, SHAPE));
  dc_m2c_u(e, PRIM + 40, 7u << 16);
  dc_m2c_u(e, SHAPE + 68, 0x1234);
  dc_m2c_vec(e, FAR, 50.0f, 50.0f, 50.0f, 1.0f);
  /* puyp-mesh: a downward probe onto the floors, then one onto the small tri. */
  dc_m2c_f(e, PUYP + 0, 1.0f); dc_m2c_u(e, PUYP + 4, 8); dc_m2c_u(e, PUYP + 8, dc_m2c_ptr(e, OUT));
  dc_m2c_vec(e, PUYP + 16, 0.5f, 5.0f, 0.25f, 1.0f); dc_m2c_vec(e, PUYP + 32, 0.0f, -10.0f, 0.0f, 0.0f);
  dc_m2c_call(e, "(method 30 collide-cache)", dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, PUYP),
              dc_m2c_ptr(e, PRIM), 0, 0, 0);
  memcpy(e->region + RESULTS, e->region + PUYP, 4);
  dc_m2c_f(e, PUYP + 0, 1.0f); dc_m2c_u(e, PUYP + 8, dc_m2c_ptr(e, OUT + 96));
  dc_m2c_vec(e, PUYP + 16, 4.5f, 5.0f, 4.75f, 1.0f);
  dc_m2c_call(e, "(method 30 collide-cache)", dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, PUYP),
              dc_m2c_ptr(e, PRIM), 0, 0, 0);
  /* resolve-moving-sphere-tri: checking direction (action 1), then not (max-u
   * negative: 2), then a max-u every tri exceeds (a miss). */
  dc_m2c_vec(e, PCORE, 0.5f, 5.0f, 0.25f, 0.5f); dc_m2c_vec(e, MOVE, 0.0f, -10.0f, 0.0f, 0.0f);
  {
    static const struct { uint32_t max_u, action; } run[3] = {{0x3f800000u, 1}, {0xbf800000u, 0}, {0x3dcccccdu, 0}};
    for (i = 0; i < 3; ++i) {
      r = dc_m2c_call(e, "(method 9 collide-cache-prim)", dc_m2c_ptr(e, PRIM), dc_m2c_ptr(e, OUT + 192 + 96 * i),
                      dc_m2c_ptr(e, PCORE), dc_m2c_ptr(e, MOVE), run[i].max_u, run[i].action);
      dc_m2c_u(e, RESULTS + 4 + 4 * i, (uint32_t)r);
    }
  }
  /* resolve-moving-sphere-sphere: within max-u, then beyond it. */
  r = dc_m2c_call(e, "(method 10 collide-cache-prim)", dc_m2c_ptr(e, PRIM), dc_m2c_ptr(e, OUT + 480),
                  dc_m2c_ptr(e, PCORE), dc_m2c_ptr(e, MOVE), 0x3f000000u, 1);
  dc_m2c_u(e, RESULTS + 16, (uint32_t)r);
  r = dc_m2c_call(e, "(method 10 collide-cache-prim)", dc_m2c_ptr(e, PRIM), dc_m2c_ptr(e, OUT + 576),
                  dc_m2c_ptr(e, PCORE), dc_m2c_ptr(e, MOVE), 0x3e4ccccdu, 1);
  dc_m2c_u(e, RESULTS + 20, (uint32_t)r);
  /* collide-puss-work: three spheres (far, near the floors' corner, overlapping
   * the prim's sphere) in a box from (-6, -1, -6) to (21, 3, 3). */
  dc_m2c_u(e, WORK + 64, (uint32_t)-6); dc_m2c_u(e, WORK + 68, (uint32_t)-1); dc_m2c_u(e, WORK + 72, (uint32_t)-6);
  dc_m2c_u(e, WORK + 80, 21); dc_m2c_u(e, WORK + 84, 3); dc_m2c_u(e, WORK + 88, 3);
  dc_m2c_cc_sphere(e, WORK + 96, 20.0f, 0.0f, 0.0f, 1.0f, box0);
  dc_m2c_cc_sphere(e, WORK + 144, -4.0f, 1.0f, -4.0f, 1.5f, box1);
  dc_m2c_cc_sphere(e, WORK + 192, 2.0f, 2.0f, 2.0f, 1.0f, box2);
  {
    static const struct { const char *method; uint32_t prim, count; } run[5] = {
      {"(method 10 collide-puss-work)", PRIM, 3}, {"(method 10 collide-puss-work)", PRIM, 2},
      {"(method 10 collide-puss-work)", FAR, 3}, {"(method 9 collide-puss-work)", PRIM, 3},
      {"(method 9 collide-puss-work)", PRIM, 1}};
    for (i = 0; i < 5; ++i) {
      dc_m2c_u(e, PARAMS + 4, run[i].count);
      r = dc_m2c_call(e, run[i].method, dc_m2c_ptr(e, WORK), dc_m2c_ptr(e, run[i].prim), dc_m2c_ptr(e, PARAMS),
                      0, 0, 0);
      mask |= (uint32_t)((uint32_t)r == e->s7 + 8) << i;
    }
  }
  return mask;  /* which puss-work calls found an overlap */
}

/* ---- collide_cache: filling the cache from a background mesh, the way
 * fill-from-background does. __pc-upload-collide-frag puts the packed vertices in
 * VU0 memory; unpack-background-collide-mesh (method 32) writes them to the
 * scratchpad, without an instance and then through one; methods 26 and 28 (box and
 * y-probe) and 27 (line-sphere, which runs method 29 through the type's method
 * table) turn the strips into cache triangles. A last fill reaches the tri limit. ---- */
static inline uint32_t dc_m2c_collide_cache_fill(dc_m2c_env *e) {
  enum { TYPE = 0x0100, MESH = 0x0204, INST = 0x0284, PACKED = 0x0300, PATS = 0x0380, WORK = 0x0400,
         SPAD = 0x8000, VERTS = 16, QWC = 6, STRIP_LEN = 17 };
  /* Strip 1 zigzags over a 4 x 2 grid; strip 2 takes a fan step (-7). */
  static const int8_t strips[STRIP_LEN + 2] = {0, 1, 2, 4, 5, 6, 7, 8, 0, 0, 2, 4, -7, 0, -128, 0, 0, 0, 0};
  static const uint8_t pat_index[8] = {0, 1, 2, 3, 0, 1, 3, 0};
  static const uint32_t pats[4] = {1, 2, 0x10, 4};  /* 0x10: the cache's ignore mask */
  static const int16_t rows[16] = {3277, 0, -2458, 0, 0, 4096, 0, 0, 2458, 0, 3277, 0, 10, 20, -30, 0};
  uint32_t i, method29 = e->mips2c_function(e, "(method 29 collide-cache)"), tris;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*collide-cache-max-tris*", 461);
  e->set_symbol(e, "*already-printed-exeeded-max-cache-tris*", e->s7 + 8);  /* no message */
  e->set_symbol(e, "*collide-work*", dc_m2c_ptr(e, WORK));
  dc_m2c_u(e, DC_M2C_CC_CACHE - 4, dc_m2c_ptr(e, TYPE));
  dc_m2c_u(e, TYPE + 132, method29);
  dc_m2c_u(e, DC_M2C_CC_CACHE + 8, 0x10);  /* ignore-mask */
  /* collide-box4w: x up to 1000, so the tris over the last two columns fall outside. */
  dc_m2c_u(e, DC_M2C_CC_CACHE + 60, (uint32_t)-3000); dc_m2c_u(e, DC_M2C_CC_CACHE + 64, (uint32_t)-1000);
  dc_m2c_u(e, DC_M2C_CC_CACHE + 68, 0);
  dc_m2c_u(e, DC_M2C_CC_CACHE + 76, 1000); dc_m2c_u(e, DC_M2C_CC_CACHE + 80, 5000); dc_m2c_u(e, DC_M2C_CC_CACHE + 84, 5000);
  /* The mesh: packed u16 vertices (16 units each from base-trans), strips, pat
   * indices. Sixteen vertices, so the unpack loop runs past its first eight; the
   * strips use the first eight. */
  for (i = 0; i < VERTS; ++i) {
    const uint16_t v[3] = {(uint16_t)(100 * (i / 2)), (uint16_t)(3 * i), (uint16_t)(100 * (i & 1))};
    memcpy(e->region + PACKED + 6 * i, v, 6);
  }
  memcpy(e->region + PACKED + 16 * QWC, strips, sizeof strips);
  memcpy(e->region + PACKED + 16 * QWC + STRIP_LEN, pat_index, sizeof pat_index);
  memcpy(e->region + PATS, pats, sizeof pats);
  dc_m2c_u(e, MESH + 0, dc_m2c_ptr(e, PACKED)); dc_m2c_u(e, MESH + 4, dc_m2c_ptr(e, PATS));
  dc_m2c_u(e, MESH + 8, 8u << 16 | STRIP_LEN);
  dc_m2c_u(e, MESH + 12, (uint32_t)-2048); dc_m2c_u(e, MESH + 16, 100); dc_m2c_u(e, MESH + 20, 1024);
  dc_m2c_u(e, MESH + 24, (QWC + 2) << 16 | QWC << 8 | VERTS);
  /* The instance: an origin, then a 4.12 rotation about y and a translation (x 64). */
  dc_m2c_vec(e, INST + 12, 100.0f, 0.0f, 200.0f, 0.0f);
  memcpy(e->region + INST + 28, rows, sizeof rows);
  /* *collide-work* for line-sphere: a box from (-5000, -5000, -5000) to (600, 5000,
   * 5000) in the space of the matrix at +48 (x scaled by a half, then moved). */
  dc_m2c_u(e, WORK + 16, (uint32_t)-5000); dc_m2c_u(e, WORK + 20, (uint32_t)-5000); dc_m2c_u(e, WORK + 24, (uint32_t)-5000);
  dc_m2c_u(e, WORK + 32, 600); dc_m2c_u(e, WORK + 36, 5000); dc_m2c_u(e, WORK + 40, 5000);
  dc_m2c_vec(e, WORK + 48, 0.5f, 0.0f, 0.0f, 0.0f); dc_m2c_vec(e, WORK + 64, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, WORK + 80, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, WORK + 96, 100.0f, 0.0f, -50.0f, 1.0f);
  {
    static const struct { const char *method; int instance; } run[3] = {
      {"(method 26 collide-cache)", 0}, {"(method 28 collide-cache)", 1}, {"(method 27 collide-cache)", 0}};
    for (i = 0; i < 4; ++i) {
      if (i == 3) e->set_symbol(e, "*collide-cache-max-tris*", 17);  /* two short of what this fill adds */
      dc_m2c_call(e, "__pc-upload-collide-frag", dc_m2c_ptr(e, PACKED), QWC, VERTS, 0, 0, 0);
      dc_m2c_call(e, "(method 32 collide-cache)", dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, MESH),
                  run[i % 3].instance ? dc_m2c_ptr(e, INST) : e->s7, 0, 0, 0);
      dc_m2c_call(e, run[i % 3].method, dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, MESH), 0, 0, 0, 0);
      memcpy(&tris, e->region + DC_M2C_CC_CACHE, 4);
      dc_m2c_u(e, DC_M2C_LOG + 0x100 + 4 * i, tris);
    }
  }
  dc_m2c_u(e, TYPE + 132, 0);  /* the native entry's address differs between the sides */
  return tris;
}

/* ---- collide_cache: foreground meshes into the cache. Methods 12 and 14 of
 * collide-shape-prim-mesh (box and y-probe) transform the mesh with its joint's
 * matrix through the mesh type's method 14, method 13 (line-sphere) through method
 * 15 with *collide-work*'s matrix too; each adds the tris inside the box and a
 * cache prim. A last fill reaches the tri limit. ---- */
static inline uint32_t dc_m2c_collide_cache_prims(dc_m2c_env *e) {
  enum { PM = 0x0204, CSHAPE = 0x0284, PROC = 0x0304, NODES = 0x0380, MATRIX = 0x0400, MESH = 0x0504,
         TYPE = 0x0600, VERTS = 0x0700, WORK = 0x0800, SPAD = 0x8000 };
  static const float verts[7][3] = {{0, 0, 0},       {4096, 0, 0},       {0, 0, 4096},     {4096, 0, 4096},
                                    {8192, 0, 0},    {8192, 1024, 4096}, {9000, 512, 2000}};
  static const uint8_t tris[4][3] = {{0, 1, 2}, {1, 3, 2}, {1, 4, 3}, {4, 5, 6}};
  static const uint32_t pats[4] = {1, 2, 0x10, 1};  /* 0x10 is ignored; tri 3 lies outside the box */
  uint32_t i, tri_count, prim_count;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*collide-cache-max-tris*", 461);
  e->set_symbol(e, "*already-printed-exeeded-max-cache-tris*", e->s7 + 8);
  e->set_symbol(e, "*collide-work*", dc_m2c_ptr(e, WORK));
  dc_m2c_u(e, DC_M2C_CC_CACHE + 8, 0x10);
  dc_m2c_u(e, DC_M2C_CC_CACHE + 60, (uint32_t)-10000); dc_m2c_u(e, DC_M2C_CC_CACHE + 64, (uint32_t)-10000);
  dc_m2c_u(e, DC_M2C_CC_CACHE + 68, (uint32_t)-10000);
  dc_m2c_u(e, DC_M2C_CC_CACHE + 76, 6000); dc_m2c_u(e, DC_M2C_CC_CACHE + 80, 10000); dc_m2c_u(e, DC_M2C_CC_CACHE + 84, 10000);
  /* The prim: its collide-shape, whose process's node list holds joint 1's matrix. */
  dc_m2c_u(e, PM + 0, dc_m2c_ptr(e, CSHAPE)); dc_m2c_b(e, PM + 8, 1);
  dc_m2c_vec(e, PM + 12, 3000.0f, 200.0f, 1000.0f, 6000.0f); dc_m2c_u(e, PM + 28, 0x55aa);
  dc_m2c_u(e, PM + 68, dc_m2c_ptr(e, MESH));
  dc_m2c_u(e, CSHAPE + 136, dc_m2c_ptr(e, PROC)); dc_m2c_u(e, PROC + 112, dc_m2c_ptr(e, NODES));
  dc_m2c_u(e, NODES + 32 + 28, dc_m2c_ptr(e, MATRIX));
  dc_m2c_vec(e, MATRIX + 0, 0.8f, 0.0f, -0.6f, 0.0f); dc_m2c_vec(e, MATRIX + 16, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, MATRIX + 32, 0.6f, 0.0f, 0.8f, 0.0f); dc_m2c_vec(e, MATRIX + 48, 1000.0f, 500.0f, -2000.0f, 1.0f);
  /* The mesh: its type's methods 14 and 15, 7 vertices, 4 tris. */
  dc_m2c_u(e, MESH - 4, dc_m2c_ptr(e, TYPE));
  dc_m2c_u(e, TYPE + 72, e->mips2c_function(e, "(method 14 collide-mesh)"));
  dc_m2c_u(e, TYPE + 76, e->mips2c_function(e, "(method 15 collide-mesh)"));
  dc_m2c_u(e, MESH + 4, 4); dc_m2c_u(e, MESH + 8, 7); dc_m2c_u(e, MESH + 12, dc_m2c_ptr(e, VERTS));
  for (i = 0; i < 7; ++i) dc_m2c_vec(e, VERTS + 16 * i, verts[i][0], verts[i][1], verts[i][2], 1.0f);
  for (i = 0; i < 4; ++i) {
    memcpy(e->region + MESH + 28 + 8 * i, tris[i], 3);
    dc_m2c_u(e, MESH + 28 + 8 * i + 4, pats[i]);
  }
  /* *collide-work* for line-sphere: a box up to x 3000 in the space of a second
   * matrix (x halved, then moved). */
  dc_m2c_u(e, WORK + 16, (uint32_t)-10000); dc_m2c_u(e, WORK + 20, (uint32_t)-10000); dc_m2c_u(e, WORK + 24, (uint32_t)-10000);
  dc_m2c_u(e, WORK + 32, 3000); dc_m2c_u(e, WORK + 36, 10000); dc_m2c_u(e, WORK + 40, 10000);
  dc_m2c_vec(e, WORK + 48, 0.5f, 0.0f, 0.0f, 0.0f); dc_m2c_vec(e, WORK + 64, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, WORK + 80, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, WORK + 96, 100.0f, 0.0f, -50.0f, 1.0f);
  {
    static const char *const method[4] = {"(method 12 collide-shape-prim-mesh)", "(method 14 collide-shape-prim-mesh)",
                                          "(method 13 collide-shape-prim-mesh)", "(method 12 collide-shape-prim-mesh)"};
    for (i = 0; i < 4; ++i) {
      /* The limit: the fill writes a seventh tri, then stops at the second without
       * storing its count or a prim, as the original does. */
      if (i == 3) e->set_symbol(e, "*collide-cache-max-tris*", 7);
      dc_m2c_call(e, method[i], dc_m2c_ptr(e, PM), dc_m2c_ptr(e, DC_M2C_CC_CACHE), 0, 0, 0, 0);
      memcpy(&tri_count, e->region + DC_M2C_CC_CACHE, 4);
      memcpy(&prim_count, e->region + DC_M2C_CC_CACHE + 4, 4);
      dc_m2c_u(e, DC_M2C_LOG + 0x100 + 4 * i, prim_count << 16 | tri_count);
    }
  }
  dc_m2c_u(e, TYPE + 72, 0); dc_m2c_u(e, TYPE + 76, 0);  /* native entries differ between the sides */
  return prim_count << 16 | tri_count;
}

/* ---- generic_effect: generic-prepare-dma-single and -double, which
 * mercneric-convert reaches through the generic work's function table. They only
 * move words: the generic work's DMA templates (11744 and up) and a fragment's
 * header and source records into a packet. Each runs three ways: source records
 * (+64) with flag +72 clear, the flag set (records from +68), and no source. ---- */
static inline uint32_t dc_m2c_prepare_dma(dc_m2c_env *e) {
  enum { SPAD = 0x4000, HDR = 0x0100, SRC = 0x0200, SRC2 = 0x0400, OUT = 0x8000, OUT_BYTES = 0x0800 };
  static const uint8_t counts[8] = {7, 12, 3, 9, 5, 1, 4, 2};
  uint32_t i, k, x = 0x9e3779b9u, end = 0;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  for (k = 0; k < 6; ++k) {
    const char *name = k < 3 ? "generic-prepare-dma-single" : "generic-prepare-dma-double";
    uint16_t verts = (uint16_t)(10 + k);
    /* Templates and tags: words that look like nothing in the region. */
    for (i = 11744; i < 12048; i += 4) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; dc_m2c_u(e, SPAD + i, x & 0x7fff7fffu); }
    for (i = 0; i < 3 * 80; i += 4) {
      dc_m2c_u(e, SRC + i, 0x51000000u | (k << 16) | i);
      dc_m2c_u(e, SRC2 + i, 0x52000000u | (k << 16) | i);
    }
    /* The fragment header: a record count byte, a vertex count, per-record counts. */
    dc_m2c_b(e, HDR + 16, 3); memcpy(e->region + HDR + 18, &verts, 2);
    memcpy(e->region + HDR + 22, counts, sizeof counts);
    dc_m2c_u(e, SPAD + 40, dc_m2c_ptr(e, OUT + OUT_BYTES * k));
    dc_m2c_u(e, SPAD + 60, dc_m2c_ptr(e, HDR));
    dc_m2c_u(e, SPAD + 64, k % 3 == 2 ? 0 : dc_m2c_ptr(e, SRC));
    dc_m2c_u(e, SPAD + 68, dc_m2c_ptr(e, SRC2));
    dc_m2c_u(e, SPAD + 72, k % 3 == 1);
    dc_m2c_u(e, SPAD + 76, 0x100); dc_m2c_u(e, SPAD + 84, 5 + k); dc_m2c_u(e, SPAD + 88, 7 * k);
    dc_m2c_call0(e, name);
    {
      uint16_t qwc;
      memcpy(&qwc, e->region + OUT + OUT_BYTES * k, 2);  /* the packet's length, stored at its start */
      end = end * 3 + qwc;
    }
  }
  return end;
}

/* ---- sky_tng: render-sky-quad after set-tex-offset, and the load-boundary
 * renderers (init-boundary-regs, render-boundary-tri and -quad, which run
 * draw-boundary-polygon), each with a vertex behind the near plane. ---- */
static inline void dc_m2c_sky_vertex(dc_m2c_env *e, uint32_t at, float x, float y, float z, float s, float t) {
  dc_m2c_vec(e, at, x, y, z, 1.0f); dc_m2c_vec(e, at + 16, s, t, 1.0f, 0.0f);
  dc_m2c_vec(e, at + 32, 128.0f - 16.0f * s, 64.0f + 32.0f * t, 32.0f, 128.0f);
}
static inline void dc_m2c_sky_dma(dc_m2c_env *e, uint32_t dma) {
  dc_m2c_u(e, dma + 0, 0x1000); dc_m2c_u(e, dma + 4, dc_m2c_ptr(e, dma + 12));
  dc_m2c_u(e, dma + 8, dc_m2c_ptr(e, dma + 12 + 0x1000));
}
static inline uint32_t dc_m2c_sky_quad_boundary(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, DATA = 0x0404, VERTS = 0x0600, DMA = 0x1004, SPAD = 0x4000, DMA_BYTES = 0x1010 };
  static const float quad[4][5] = {{-1, -1, 4, 0, 0}, {3, -1, 5, 1, 0}, {3, 2, -3, 1, 1}, {-1, 2, 6, 0, 1}};
  uint32_t i, k, total = 0, base_after;
  dc_m2c_sky_camera(e, CAMERA);
  e->set_symbol(e, "*sky-tng-data*", dc_m2c_ptr(e, DATA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  for (i = 0; i < 4; ++i) dc_m2c_sky_vertex(e, VERTS + 48 * i, quad[i][0], quad[i][1], quad[i][2], quad[i][3], quad[i][4]);
  for (k = 0; k < 3; ++k) dc_m2c_sky_dma(e, DMA + DMA_BYTES * k);
  dc_m2c_call0(e, "init-sky-regs");
  dc_m2c_call(e, "set-tex-offset", 0x8000, 0x4000, 0, 0, 0, 0);  /* a half and a quarter in 16.16 */
  dc_m2c_call(e, "render-sky-quad", dc_m2c_ptr(e, VERTS), dc_m2c_ptr(e, DMA), 0, 0, 0, 0);
  dc_m2c_call0(e, "init-boundary-regs");
  dc_m2c_call(e, "render-boundary-tri", dc_m2c_ptr(e, VERTS), dc_m2c_ptr(e, DMA + DMA_BYTES), 0, 0, 0, 0);
  dc_m2c_call(e, "render-boundary-quad", dc_m2c_ptr(e, VERTS), dc_m2c_ptr(e, DMA + 2 * DMA_BYTES), 0, 0, 0, 0);
  for (k = 0; k < 3; ++k) {
    memcpy(&base_after, e->region + DMA + DMA_BYTES * k + 4, 4);
    total = total << 10 | (base_after - dc_m2c_ptr(e, DMA + DMA_BYTES * k + 12));
  }
  return total;  /* packet bytes written by each, 10 bits apiece */
}

/* ---- collide_mesh: should-push-away-test and sphere-on-platform-test (methods 11
 * and 12) over six cache tris around a sphere. closest-pt-in-triangle stands in
 * with the point below or above the sphere's centre at the tri's first vertex's
 * height, which is the true closest point for the level tris here. ---- */
static uint64_t dc_m2c_cm_closest_pt(dc_m2c_env *e, const uint64_t *a) {
  unsigned char *out = (unsigned char *)e->memory(e, (uint32_t)a[0]);
  const unsigned char *sphere = e->region + dc_m2c_off(e, a[1]), *tri = e->region + dc_m2c_off(e, a[2]);
  static const float one = 1.0f;
  dc_m2c_log(e, 0xc1 << 24 | dc_m2c_off(e, a[2]));
  memcpy(out, sphere, 4); memcpy(out + 4, tri + 4, 4); memcpy(out + 8, sphere + 8, 4); memcpy(out + 12, &one, 4);
  return 0;
}
static inline void dc_m2c_cm_tri(dc_m2c_env *e, uint32_t at, const float v[9], float nx, float ny, float nz,
                                 uint32_t pat) {
  unsigned k;
  int32_t lo, hi;
  for (k = 0; k < 3; ++k) dc_m2c_vec(e, at + 16 * k, v[3 * k], v[3 * k + 1], v[3 * k + 2], 1.0f);
  dc_m2c_vec(e, at + 48, nx, ny, nz, 0.0f); dc_m2c_u(e, at + 60, pat);
  for (k = 0; k < 3; ++k) {  /* bbox4w: the vertices' integer extent */
    float a0 = v[k], a1 = v[3 + k], a2 = v[6 + k];
    float mn = a0 < a1 ? (a0 < a2 ? a0 : a2) : (a1 < a2 ? a1 : a2), mx = a0 > a1 ? (a0 > a2 ? a0 : a2) : (a1 > a2 ? a1 : a2);
    lo = (int32_t)mn; hi = (int32_t)mx;
    dc_m2c_u(e, at + 64 + 4 * k, (uint32_t)lo); dc_m2c_u(e, at + 80 + 4 * k, (uint32_t)hi);
  }
}
static inline uint32_t dc_m2c_collide_mesh_tests(dc_m2c_env *e) {
  enum { MESH = 0x0104, SPHERE = 0x0200, TRIS = 0x1000, OUT = 0x2000 };
  /* A floor below the sphere (penetrating), the ground, a floor just under the
   * centre with a pat the platform test skips, a wall, a far floor, a ceiling. */
  static const float floor60[9] = {-200, 60, -200, -200, 60, 200, 200, 60, -200},
                     ground[9] = {-200, 0, -200, -200, 0, 200, 200, 0, -200},
                     floor90[9] = {-200, 90, -200, -200, 90, 200, 200, 90, -200},
                     wall[9] = {30, 120, -200, 30, 120, 200, 30, -200, 0},
                     far_floor[9] = {1000, 80, 1000, 1000, 80, 1200, 1200, 80, 1000},
                     ceiling[9] = {-200, 140, -200, 200, 140, -200, -200, 140, 200};
  uint32_t r[4], i;
  e->set_symbol(e, "closest-pt-in-triangle", dc_m2c_goal(e, 0, dc_m2c_cm_closest_pt));
  dc_m2c_cm_tri(e, TRIS + 0, floor60, 0, 1, 0, 0);
  dc_m2c_cm_tri(e, TRIS + 96, ground, 0, 1, 0, 16);
  dc_m2c_cm_tri(e, TRIS + 192, floor90, 0, 1, 0, 8);
  dc_m2c_cm_tri(e, TRIS + 288, wall, 1, 0, 0, 0);
  dc_m2c_cm_tri(e, TRIS + 384, far_floor, 0, 1, 0, 0);
  dc_m2c_cm_tri(e, TRIS + 480, ceiling, 0, -1, 0, 0);
  dc_m2c_u(e, MESH + 4, 6);
  dc_m2c_vec(e, SPHERE, 0.0f, 100.0f, 0.0f, 50.0f);
  r[0] = (uint32_t)dc_m2c_call(e, "(method 11 collide-mesh)", dc_m2c_ptr(e, MESH), dc_m2c_ptr(e, TRIS),
                               dc_m2c_ptr(e, OUT), dc_m2c_ptr(e, SPHERE), dc_m2c_fbits(0.0f), 0);
  r[1] = (uint32_t)dc_m2c_call(e, "(method 12 collide-mesh)", dc_m2c_ptr(e, MESH), dc_m2c_ptr(e, TRIS),
                               dc_m2c_ptr(e, OUT + 96), dc_m2c_ptr(e, SPHERE), dc_m2c_fbits(200.0f), 0);
  /* Raised to 160: the platform test takes the floor at 60 (50 below the sphere)
   * over the ground (110), the ceiling faces it the wrong way, and nothing
   * penetrates for the push-away test. */
  dc_m2c_vec(e, SPHERE, 0.0f, 160.0f, 0.0f, 50.0f);
  r[2] = (uint32_t)dc_m2c_call(e, "(method 12 collide-mesh)", dc_m2c_ptr(e, MESH), dc_m2c_ptr(e, TRIS),
                               dc_m2c_ptr(e, OUT + 192), dc_m2c_ptr(e, SPHERE), dc_m2c_fbits(200.0f), 0);
  r[3] = (uint32_t)dc_m2c_call(e, "(method 11 collide-mesh)", dc_m2c_ptr(e, MESH), dc_m2c_ptr(e, TRIS),
                               dc_m2c_ptr(e, OUT + 288), dc_m2c_ptr(e, SPHERE), dc_m2c_fbits(0.0f), 0);
  for (i = 0; i < 4; ++i) dc_m2c_u(e, OUT + 0x400 + 4 * i, r[i]);
  return r[0] ^ r[1] ^ r[2] ^ r[3];
}

/* ---- generic_effect and generic_effect2: generic-envmap-proc, generic-envmap-dproc
 * and generic-interp-dproc over one 64-entry vertex table (32 bytes each: a
 * position, then a normal whose w the envmap dproc divides by). ---- */
static inline uint32_t dc_m2c_generic_envmap_interp(dc_m2c_env *e) {
  enum { SPAD = 0x4000, STREAM = 0x0100, INTERP = 0x0140, INDICES = 0x0200, LERP = 0x0300, TABLE = 0x1000,
         OUT1 = 0x2000, OUT2 = 0x2800, COUNT = 12 };
  uint32_t i, state = 0x13572468u, crc = 0;
  int16_t h;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  for (i = 0; i < 64; ++i) {
    float nx = dc_m2c_rand(&state), ny = dc_m2c_rand(&state), nz = dc_m2c_rand(&state);
    dc_m2c_rand_vec(e, TABLE + 32 * i, &state, 1.0f);
    /* A normal well away from zero; w (byte 24 of the entry is z) stays off 1. */
    dc_m2c_vec(e, TABLE + 32 * i + 16, nx + (nx < 0 ? -1.0f : 1.0f), ny + (ny < 0 ? -1.0f : 1.0f),
               2.0f + (nz < 0 ? -nz : nz) / 8.0f, 1.0f);
  }
  for (i = 0; i < COUNT + 8; ++i) {
    uint16_t index = (uint16_t)(((i * 11u + 2u) & 63u) | ((i % 3u == 1u) << 8));
    memcpy(e->region + INDICES + 2 * i, &index, 2);
  }
  dc_m2c_u(e, SPAD + 60, dc_m2c_ptr(e, STREAM));
  dc_m2c_u(e, STREAM + 0, dc_m2c_ptr(e, INDICES)); dc_m2c_u(e, STREAM + 4, dc_m2c_ptr(e, TABLE));
  {
    uint16_t count = COUNT;
    memcpy(e->region + STREAM + 20, &count, 2);
  }
  dc_m2c_vec(e, SPAD + 12016, 0.5f, 0.25f, 64.0f, 2048.0f);
  /* envmap-proc: fills OUT1 with the template at 12048, then writes per index. */
  dc_m2c_u(e, SPAD + 52, COUNT);
  dc_m2c_u(e, SPAD + 32, dc_m2c_ptr(e, OUT1)); dc_m2c_u(e, SPAD + 36, dc_m2c_ptr(e, OUT2));
  dc_m2c_u(e, SPAD + 12048, 0x01020304u); dc_m2c_u(e, SPAD + 12052, 0x05060708u);
  dc_m2c_u(e, SPAD + 12056, 0x090a0b0cu); dc_m2c_u(e, SPAD + 12060, 0x0d0e0f10u);
  dc_m2c_call0(e, "generic-envmap-proc");
  /* envmap-dproc: rewrites the table in place; the word at 12048 goes into each
   * vertex's +20. */
  dc_m2c_u(e, SPAD + 12048, 0x00c0ffeeu);
  dc_m2c_call0(e, "generic-envmap-dproc");
  /* interp-dproc: 16 vertices from entry 8 blended with the ones their index
   * bytes name, weights 0x60 and 0xa0. */
  dc_m2c_u(e, SPAD + 80, dc_m2c_ptr(e, INTERP));
  h = 0; memcpy(e->region + INTERP + 0, &h, 2);
  h = 12; memcpy(e->region + INTERP + 2, &h, 2);
  h = 8; memcpy(e->region + INTERP + 4, &h, 2);
  dc_m2c_u(e, INTERP + 8, dc_m2c_ptr(e, LERP));
  h = 0x60; memcpy(e->region + INTERP + 12, &h, 2);
  h = 0xa0; memcpy(e->region + INTERP + 14, &h, 2);
  for (i = 0; i < 40; ++i) dc_m2c_b(e, LERP + i, (uint8_t)((i * 7u + 3u) & 63u));
  dc_m2c_call0(e, "generic-interp-dproc");
  for (i = 0; i < 64; ++i) {
    uint32_t w;
    memcpy(&w, e->region + TABLE + 32 * i + 16, 4);
    crc = (crc << 1 | crc >> 31) ^ w;
  }
  return crc;  /* the table's +16 words after both dprocs */
}

/* ---- shadow: shadow-execute over a chain of two shadow objects. The first is
 * the tetrahedron again plus a double-sided strip (five double tris, eight double
 * edges); the second is chained by its cnt tag's upper word and carries the
 * skip flag (32). ---- */
static inline uint32_t dc_m2c_shadow_double(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, SDATA = 0x0600, CHAIN = 0x1000, CHAIN2 = 0x2000, GSF = 0x8000, OUT = 0xa000,
         SPAD = 0x4000, GEO_BYTES = 720 };
  /* Eleven vertices (the last in no tri), so shadow-add-verts' tail copies three. */
  static const float vert[11][3] = {{0, 0, 0}, {2, 0, 0}, {0, 0, 2}, {0, 2, 0},  {3, 1, -1}, {5, 1, -1},
                                    {5, 3, 0}, {3, 3, 0}, {7, 1, -1}, {7, 3, 0}, {8, 2, 1}};
  static const uint8_t tris[16] = {0, 1, 2, 0, 0, 3, 1, 0, 1, 3, 2, 0, 2, 3, 0, 0};
  static const uint8_t edges[24] = {0, 1, 0, 1, 1, 2, 0, 2, 2, 0, 0, 3, 0, 3, 1, 3, 3, 1, 1, 2, 3, 2, 2, 3};
  /* Five double tris, so the four-at-a-time loop runs once and the tail once. */
  static const uint8_t double_tris[20] = {4, 5, 6, 0, 4, 6, 7, 0, 5, 8, 9, 0, 5, 9, 6, 0, 6, 9, 7, 0};
  static const uint8_t double_edges[32] = {4, 5, 0, 0, 5, 6, 0, 3, 6, 7, 1, 4, 7, 4, 1, 1,
                                           4, 6, 0, 1, 5, 8, 2, 2, 8, 9, 2, 2, 9, 6, 3, 4};
  static const uint16_t counts[6] = {11, 0, 4, 6, 5, 8};
  static const uint32_t offsets[6] = {400, 576, 608, 624, 656, 688};
  uint32_t i, k, state = 0xd0b1eu;
  uint64_t end;
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*gsf-buffer*", dc_m2c_ptr(e, GSF));
  e->set_symbol(e, "flush-cache", dc_m2c_goal(e, 0, dc_m2c_flush_cache));
  e->set_symbol(e, "camera-pos", dc_m2c_goal(e, 1, dc_m2c_camera_pos));
  e->set_symbol(e, "*shadow-data*", dc_m2c_ptr(e, SDATA));
  for (i = 0; i < 64; ++i) dc_m2c_u(e, SDATA + 4 * i, 0x10000000u * (i & 7) + 0x101u * i);
  dc_m2c_f(e, CAMERA + 0, 1.0f);
  for (i = 0; i < 16; ++i) dc_m2c_f(e, CAMERA + 364 + 4 * i, (i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.0625f);
  dc_m2c_vec(e, DC_M2C_SHADOW_CAMPOS, 0.0f, 10.0f, 20.0f, 1.0f);
  for (k = 0; k < 2; ++k) {
    const uint32_t chain = k ? CHAIN2 : CHAIN, settings = chain + 16, geo = chain + 112;
    dc_m2c_u(e, chain + 0, 1u << 28 | (96 + GEO_BYTES) / 16);
    dc_m2c_u(e, chain + 12, k ? 0 : dc_m2c_ptr(e, CHAIN2));  /* the next object */
    dc_m2c_u(e, chain + 16 + 96 + GEO_BYTES, 7u << 28);
    dc_m2c_vec(e, settings + 0, 0.5f, 3.0f, 0.5f, 0.0f); dc_m2c_u(e, settings + 12, k ? 32 : 1 | 8);
    dc_m2c_vec(e, settings + 16, 0.25f, -1.0f, 0.125f, 8.0f);
    dc_m2c_vec(e, settings + 32, 0.0f, 1.0f, 0.0f, -0.5f);
    dc_m2c_vec(e, settings + 48, 0.0f, 1.0f, 0.0f, 4.0f);
    dc_m2c_vec(e, settings + 64, 0.0f, -1.0f, 0.0f, 6.0f);
    for (i = 0; i < 6; ++i) {
      memcpy(e->region + geo + 8 + 2 * i, &counts[i], 2);
      dc_m2c_u(e, geo + 20 + 4 * i, offsets[i]);
    }
    for (i = 0; i < 2 * 32; ++i)
      dc_m2c_f(e, geo + 144 + 4 * i, (i % 32 < 16) ? ((i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.125f) : 0.0f);
    for (i = 0; i < 11; ++i) {
      dc_m2c_vec(e, geo + 400 + 16 * i, vert[i][0], vert[i][1], vert[i][2], 1.0f);
      dc_m2c_b(e, geo + 576 + 2 * i, (uint8_t)(i & 1));
    }
    memcpy(e->region + geo + 608, tris, sizeof tris);
    memcpy(e->region + geo + 624, edges, sizeof edges);
    memcpy(e->region + geo + 656, double_tris, sizeof double_tris);
    memcpy(e->region + geo + 688, double_edges, sizeof double_edges);
  }
  end = dc_m2c_call(e, "shadow-execute", dc_m2c_ptr(e, CHAIN), dc_m2c_ptr(e, OUT), 0, 0, 0, 0);
  return dc_m2c_off(e, end) - OUT;  /* packet bytes */
}

/* ---- sparticle_launcher: sp-launch-particles-var's other paths. A 3d system
 * with a launch state (a counter, a rotation, a group id) and a launch control
 * whose second entry matches the group; flags 128 (rotation through cos, sin and
 * quaternion-axis-angle!), 256 (the sprite aux list) and 512; a sound spec; the
 * launcher in level 1's heap. Before that, a call while *sp-launcher-enable* is
 * #f and one while *sp-launcher-lock* is set, which queues the launch. ---- */
/* Their stack arguments (the launch info on sp-launch-particles-var's stack) differ
 * between the sides, so they log only region addresses and values. */
static uint64_t dc_m2c_sp_queue(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x9a << 24 | dc_m2c_off(e, a[0])); dc_m2c_log(e, dc_m2c_off(e, a[1]));
  return 0;
}
static uint64_t dc_m2c_sp_euler(dc_m2c_env *e, const uint64_t *a) { dc_m2c_log(e, 0xe1 << 24 | dc_m2c_off(e, a[1])); return 0; }
static uint64_t dc_m2c_sp_rotate(dc_m2c_env *e, const uint64_t *a) { dc_m2c_log(e, 0x40 << 24 | dc_m2c_off(e, a[1])); return 0; }
static uint64_t dc_m2c_sp_cos(dc_m2c_env *e, const uint64_t *a) { dc_m2c_log(e, (uint32_t)a[0]); return 0x3f5db3d7u; }
static uint64_t dc_m2c_sp_sin(dc_m2c_env *e, const uint64_t *a) { dc_m2c_log(e, (uint32_t)a[0]); return 0x3f000000u; }
static uint64_t dc_m2c_sp_quat(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x9b << 24 | dc_m2c_off(e, a[0]));
  dc_m2c_log(e, (uint32_t)a[1]); dc_m2c_log(e, (uint32_t)a[2]); dc_m2c_log(e, (uint32_t)a[3]);
  memcpy(e->region + dc_m2c_off(e, a[0]), &a[1], 4);  /* the quaternion's x: the cos it was given */
  return a[0];
}
static uint64_t dc_m2c_sp_sound_id(dc_m2c_env *e, const uint64_t *a) { (void)a; dc_m2c_log(e, 0x5d << 24); return 0x1234; }
static uint64_t dc_m2c_sp_sound(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x5e << 24 | dc_m2c_off(e, a[0])); dc_m2c_log(e, (uint32_t)a[1]);
  return 0;
}
static inline uint32_t dc_m2c_launch_3d(dc_m2c_env *e) {
  enum { SYSTEM = 0x0104, LAUNCHER = 0x0204, POS = 0x0300, SPECS = 0x0400, LEVEL = 0x0800, STATE = 0x0f04,
         CONTROL = 0x1004, GROUP = 0x1104, ID = 0x1184, NO_ID = 0x11a4, ROT = 0x11c4, WORD = 0x11e4,
         SOUND = 0x1204, TOD = 0x1804, CACHE = 0x3004, SPRITES = 0x5000, ADGIFS = 0x5400 };
  static const struct { uint16_t field, flags; uint32_t value; } spec[] = {
    {1, 0, 0x00123400u}, {4, 4, 0}, {6, 1, 0x40000000u},  /* texture, birth, num 2.0 */
    {7, 0, 0},                                              /* sound spec: below */
    {10, 1, 0x3e800000u}, {11, 1, 0x3f400000u}, {12, 1, 0xbf000000u}, {13, 1, 0x3f000000u}, {17, 3, (uint32_t)-4},
    {18, 1, 0x42800000u}, {21, 1, 0x43000000u}, {25, 1, 0x3e000000u}, {46, 0, 150},
    {47, 0, 128 | 256 | 512 | 16},                          /* rotation, aux list, 512, adgif 66 */
    {54, 1, 0x3dcccccdu}, {70, 0, 0}};
  uint32_t i, used;
  e->set_symbol(e, "*level*", dc_m2c_ptr(e, LEVEL));
  e->set_symbol(e, "*time-of-day-context*", dc_m2c_ptr(e, TOD));
  e->set_symbol(e, "*particle-adgif-cache*", dc_m2c_ptr(e, CACHE));
  e->set_symbol(e, "sp-init-fields!", dc_m2c_goal(e, 0, dc_m2c_sp_init_fields));
  e->set_symbol(e, "sp-get-particle", dc_m2c_goal(e, 1, dc_m2c_sp_get_particle));
  e->set_symbol(e, "sp-adjust-launch", dc_m2c_goal(e, 2, dc_m2c_sp_adjust_launch));
  e->set_symbol(e, "particle-setup-adgif", dc_m2c_goal(e, 3, dc_m2c_setup_adgif));
  e->set_symbol(e, "particle-adgif", e->mips2c_function(e, "particle-adgif"));
  e->set_symbol(e, "sp-euler-convert", dc_m2c_goal(e, 5, dc_m2c_sp_euler));
  e->set_symbol(e, "sp-rotate-system", dc_m2c_goal(e, 6, dc_m2c_sp_rotate));
  e->set_symbol(e, "cos", dc_m2c_goal(e, 7, dc_m2c_sp_cos));
  e->set_symbol(e, "sin", dc_m2c_goal(e, 8, dc_m2c_sp_sin));
  e->set_symbol(e, "quaternion-axis-angle!", dc_m2c_goal(e, 9, dc_m2c_sp_quat));
  e->set_symbol(e, "new-sound-id", dc_m2c_goal(e, 10, dc_m2c_sp_sound_id));
  /* Stored, never called: the birth stand-in's slot gives it a marked address. */
  e->set_symbol(e, "add-to-sprite-aux-list", dc_m2c_goal(e, 4, dc_m2c_sp_birth));
  for (i = 0; i < sizeof spec / sizeof spec[0]; ++i) {
    memcpy(e->region + SPECS + 16 * i, &spec[i].field, 2);
    memcpy(e->region + SPECS + 16 * i + 2, &spec[i].flags, 2);
    dc_m2c_u(e, SPECS + 16 * i + 4, spec[i].field == 4 ? dc_m2c_goal(e, 4, dc_m2c_sp_birth)
                                    : spec[i].field == 7 ? dc_m2c_ptr(e, SOUND) : spec[i].value);
  }
  dc_m2c_f(e, SOUND + 4, 1.5f);
  dc_m2c_u(e, SYSTEM + 24, 1);  /* 3d */
  dc_m2c_u(e, LAUNCHER + 8, dc_m2c_ptr(e, SPECS));
  dc_m2c_vec(e, POS, -4.0f, 8.0f, 2.0f, 1.0f);
  /* Level 0's heap elsewhere, level 1's around the launcher. */
  dc_m2c_u(e, LEVEL + 124, dc_m2c_ptr(e, 0x8000)); dc_m2c_u(e, LEVEL + 128, dc_m2c_ptr(e, 0x9000));
  dc_m2c_u(e, LEVEL + 2732, dc_m2c_ptr(e, 0)); dc_m2c_u(e, LEVEL + 2736, dc_m2c_ptr(e, 0x1000));
  dc_m2c_vec(e, TOD + 108, 1.0f, 0.5f, 0.25f, 1.0f);
  for (i = 0; i < DC_M2C_LAUNCH_POOL_SIZE; ++i) {
    uint32_t cpu = DC_M2C_LAUNCH_POOL + 0x10 + 144 * i;
    dc_m2c_u(e, cpu + 0, dc_m2c_ptr(e, SPRITES + 48 * i));
    dc_m2c_u(e, cpu + 4, dc_m2c_ptr(e, ADGIFS + 0x60 * i));
  }
  /* The launch state: group id 0x77 (through +0), a rotation, a word, a counter. */
  dc_m2c_u(e, STATE + 0, dc_m2c_ptr(e, ID)); dc_m2c_u(e, ID + 24, 0x77);
  dc_m2c_u(e, STATE + 12, dc_m2c_ptr(e, ROT)); dc_m2c_u(e, STATE + 16, dc_m2c_ptr(e, WORD));
  dc_m2c_u(e, WORD, 0xabc);
  /* The launch control: a group with flags 4 and 8, two entries, the second ours. */
  dc_m2c_u(e, CONTROL + 0, 2); dc_m2c_u(e, CONTROL + 12, dc_m2c_ptr(e, GROUP)); dc_m2c_u(e, CONTROL + 28, 5);
  {
    uint16_t flags = 4 | 8;
    memcpy(e->region + GROUP + 6, &flags, 2);
  }
  dc_m2c_u(e, NO_ID, 0x99);
  dc_m2c_u(e, CONTROL + 60, dc_m2c_ptr(e, NO_ID)); dc_m2c_u(e, CONTROL + 92, dc_m2c_ptr(e, ID + 24));
  {
    /* Disabled, then locked (queued): both leave before launching anything. */
    const uint64_t plain[8] = {dc_m2c_ptr(e, SYSTEM), dc_m2c_ptr(e, LAUNCHER), dc_m2c_ptr(e, POS), e->s7, e->s7,
                               dc_m2c_fbits(1.0f), 0, 0};
    const uint64_t args[8] = {dc_m2c_ptr(e, SYSTEM), dc_m2c_ptr(e, LAUNCHER), dc_m2c_ptr(e, POS),
                              dc_m2c_ptr(e, STATE), dc_m2c_ptr(e, CONTROL), dc_m2c_fbits(1.0f), 0, 0};
    e->set_symbol(e, "sp-queue-launch", dc_m2c_goal(e, 11, dc_m2c_sp_queue));
    e->set_symbol(e, "*sp-launcher-enable*", e->s7);
    e->call(e, "sp-launch-particles-var", plain);
    e->set_symbol(e, "*sp-launcher-enable*", e->s7 + 8);
    e->set_symbol(e, "*sp-launcher-lock*", e->s7 + 8);
    e->call(e, "sp-launch-particles-var", plain);
    e->set_symbol(e, "*sp-launcher-lock*", e->s7);
    e->set_symbol(e, "sound-play-by-spec", dc_m2c_goal(e, 11, dc_m2c_sp_sound));
    e->call(e, "sp-launch-particles-var", args);
  }
  memcpy(&used, e->region + DC_M2C_LAUNCH_POOL, 4);
  return used;  /* particles launched */
}

/* ---- collide_func: collide-do-primitives (a sphere swept past a triangle's
 * vertices and edges), called directly and through
 * moving-sphere-triangle-intersect when the face point falls outside the tri.
 * ray-sphere-intersect and ray-cylinder-intersect stand in, with a u for each
 * vertex and edge by where it sits in its tri; the cylinder reports the edge's
 * start as the contact point. ---- */
enum { DC_M2C_PRIM_TRI = 0x0300, DC_M2C_PRIM_TRI2 = 0x0340 };
static uint32_t dc_m2c_prim_u(dc_m2c_env *e, uint64_t at, const uint32_t near_u[3], const uint32_t far_u[3]) {
  uint32_t off = dc_m2c_off(e, at);
  return off >= DC_M2C_PRIM_TRI2 ? far_u[(off - DC_M2C_PRIM_TRI2) / 16 % 3] : near_u[(off - DC_M2C_PRIM_TRI) / 16 % 3];
}
static uint64_t dc_m2c_ray_sphere(dc_m2c_env *e, const uint64_t *a) {
  static const uint32_t near_u[3] = {0x3f666666u, 0x3ecccccdu, 0x40000000u};  /* 0.9, 0.4, 2 */
  static const uint32_t far_u[3] = {0xbf800000u, 0x3fc00000u, 0xbf000000u};   /* -1, 1.5, -0.5 */
  dc_m2c_log(e, 0x5f << 24 | dc_m2c_off(e, a[2]));
  return dc_m2c_prim_u(e, a[2], near_u, far_u);
}
static uint64_t dc_m2c_ray_cylinder(dc_m2c_env *e, const uint64_t *a) {
  static const uint32_t near_u[3] = {0x3f333333u, 0x3e99999au, 0xbf800000u};  /* 0.7, 0.3, -1 */
  static const uint32_t far_u[3] = {0x40400000u, 0xbe800000u, 0x3f900000u};   /* 3, -0.25, 1.125 */
  dc_m2c_log(e, 0xcc << 24 | dc_m2c_off(e, a[2]));
  memcpy(e->region + dc_m2c_off(e, a[6]), e->region + dc_m2c_off(e, a[2]), 16);
  return dc_m2c_prim_u(e, a[2], near_u, far_u);
}
static inline uint32_t dc_m2c_collide_primitives(dc_m2c_env *e) {
  enum { SPHERE = 0x0100, MOVE = 0x0200, OUT = 0x0400, NORMAL = 0x0500, RESULT = 0x0600 };
  uint32_t i, r[6];
  e->set_symbol(e, "ray-sphere-intersect", dc_m2c_goal(e, 0, dc_m2c_ray_sphere));
  e->set_symbol(e, "ray-cylinder-intersect", dc_m2c_goal(e, 1, dc_m2c_ray_cylinder));
  e->set_symbol(e, "collide-do-primitives", e->mips2c_function(e, "collide-do-primitives"));
  dc_m2c_vec(e, DC_M2C_PRIM_TRI + 0, -5.0f, -5.0f, 0.0f, 1.0f);
  dc_m2c_vec(e, DC_M2C_PRIM_TRI + 16, 5.0f, -5.0f, 0.0f, 1.0f);
  dc_m2c_vec(e, DC_M2C_PRIM_TRI + 32, 0.0f, 5.0f, 0.0f, 1.0f);
  for (i = 0; i < 3; ++i) memcpy(e->region + DC_M2C_PRIM_TRI2 + 16 * i, e->region + DC_M2C_PRIM_TRI + 16 * i, 16);
  /* Beside the tri's right edge, moving straight at its plane. */
  dc_m2c_vec(e, SPHERE, 4.0f, 2.0f, 10.0f, 1.0f);
  dc_m2c_vec(e, MOVE, 0.0f, 0.0f, -20.0f, 0.0f);
  r[0] = (uint32_t)dc_m2c_call(e, "collide-do-primitives", dc_m2c_ptr(e, SPHERE), dc_m2c_ptr(e, MOVE),
                               dc_m2c_fbits(1.0f), dc_m2c_ptr(e, DC_M2C_PRIM_TRI), dc_m2c_ptr(e, OUT), 0);
  {
    const uint64_t args[8] = {dc_m2c_ptr(e, SPHERE), dc_m2c_ptr(e, MOVE), dc_m2c_fbits(1.0f),
                              dc_m2c_ptr(e, DC_M2C_PRIM_TRI), dc_m2c_ptr(e, OUT + 16), dc_m2c_ptr(e, NORMAL), 0, 0};
    r[1] = (uint32_t)e->call_jalr(e, "moving-sphere-triangle-intersect", args);
  }
  r[2] = (uint32_t)dc_m2c_call(e, "collide-do-primitives", dc_m2c_ptr(e, SPHERE), dc_m2c_ptr(e, MOVE),
                               dc_m2c_fbits(1.0f), dc_m2c_ptr(e, DC_M2C_PRIM_TRI2), dc_m2c_ptr(e, OUT + 32), 0);
  /* Starting already touching the plane: inside the tri, beside its edge (which
   * jumps to collide-do-primitives again), and from behind. */
  {
    static const float start[3][3] = {{0.5f, -1.0f, 0.5f}, {4.0f, 2.0f, 0.5f}, {0.0f, -1.0f, -0.5f}};
    for (i = 0; i < 3; ++i) {
      dc_m2c_vec(e, SPHERE + 16 * (i + 1), start[i][0], start[i][1], start[i][2], 1.0f);
      {
        const uint64_t args[8] = {dc_m2c_ptr(e, SPHERE + 16 * (i + 1)), dc_m2c_ptr(e, MOVE), dc_m2c_fbits(1.0f),
                                  dc_m2c_ptr(e, DC_M2C_PRIM_TRI), dc_m2c_ptr(e, OUT + 48 + 16 * i),
                                  dc_m2c_ptr(e, NORMAL + 16 * (i + 1)), 0, 0};
        r[3 + i] = (uint32_t)e->call_jalr(e, "moving-sphere-triangle-intersect", args);
      }
    }
  }
  for (i = 0; i < 6; ++i) dc_m2c_u(e, RESULT + 4 * i, r[i]);
  return r[0] ^ r[1] ^ r[2] ^ r[3] ^ r[4] ^ r[5];
}

/* ---- ocean_vu0: ocean-generate-verts, which draw-ocean runs every frame, over a
 * wave-info of small random heights. upload-vu0-program and
 * vu-lights<-light-group! stand in (the second copies the time-of-day lights);
 * *ocean-generate-verts-vector* is all ones, so vector*! stands in as a copy. ---- */
static uint64_t dc_m2c_ocean_upload(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x0b << 24 | dc_m2c_off(e, a[1])); return 0;
}
static uint64_t dc_m2c_ocean_lights(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x11 << 24 | dc_m2c_off(e, a[0]));
  memmove(e->region + dc_m2c_off(e, a[0]), e->region + dc_m2c_off(e, a[1]), 112);
  return 0;
}
static uint64_t dc_m2c_vector_times_ones(dc_m2c_env *e, const uint64_t *a) {
  dc_m2c_log(e, 0x7e << 24 | dc_m2c_off(e, a[0]));
  memmove(e->region + dc_m2c_off(e, a[0]), e->region + dc_m2c_off(e, a[1]), 16);
  return a[0];
}
static inline uint32_t dc_m2c_ocean_verts(dc_m2c_env *e) {
  enum { WORK = 0x0100, ONES = 0x0200, TOD = 0x0300, WAVES = 0x1000, VERTS = 0x2000, VERT_BYTES = 0x8000 };
  uint32_t i, state = 0x0cea4u, sum = 0;
  e->set_symbol(e, "*ocean-vu0-work*", dc_m2c_ptr(e, WORK));
  e->set_symbol(e, "*time-of-day-context*", dc_m2c_ptr(e, TOD));
  e->set_symbol(e, "*ocean-generate-verts-vector*", dc_m2c_ptr(e, ONES));
  e->set_symbol(e, "upload-vu0-program", dc_m2c_goal(e, 0, dc_m2c_ocean_upload));
  e->set_symbol(e, "vu-lights<-light-group!", dc_m2c_goal(e, 1, dc_m2c_ocean_lights));
  e->set_symbol(e, "vector*!", dc_m2c_goal(e, 2, dc_m2c_vector_times_ones));
  dc_m2c_vec(e, ONES, 1.0f, 1.0f, 1.0f, 1.0f);
  dc_m2c_vec(e, WORK + 0, 0.125f, 255.0f, 0.25f, 1.0f);  /* slope scale, clamp */
  /* The lights: three direction rows, then colours and ambient. */
  dc_m2c_vec(e, TOD + 156, 0.0f, -0.8f, 0.6f, 0.0f); dc_m2c_vec(e, TOD + 172, 0.6f, 0.0f, 0.8f, 0.0f);
  dc_m2c_vec(e, TOD + 188, 0.0f, 0.6f, -0.8f, 0.0f);
  dc_m2c_vec(e, TOD + 204, 96.0f, 80.0f, 64.0f, 0.0f); dc_m2c_vec(e, TOD + 220, 32.0f, 48.0f, 64.0f, 0.0f);
  dc_m2c_vec(e, TOD + 236, 16.0f, 16.0f, 24.0f, 0.0f); dc_m2c_vec(e, TOD + 252, 40.0f, 40.0f, 48.0f, 128.0f);
  for (i = 0; i < 4096; i += 4) dc_m2c_f(e, WAVES + i, dc_m2c_rand(&state) * 0.25f);
  dc_m2c_call(e, "ocean-generate-verts", dc_m2c_ptr(e, VERTS), dc_m2c_ptr(e, WAVES), 0, 0, 0, 0);
  for (i = 0; i < VERT_BYTES; i += 4) {
    uint32_t w;
    memcpy(&w, e->region + VERTS + i, 4);
    sum += w != 0;
  }
  return sum;  /* vertex words written */
}

/* ---- merc_blend_shape: blerc-execute over the chain of one fragment that
 * setup-blerc-chains-for-one-fragment links from *blerc-globals*, with patterned
 * blend-shape data and flush-cache standing in. The PC port skips blerc-execute
 * while *use-fp-blerc* is #t (its default) and blends in the renderer. ---- */
static inline uint32_t dc_m2c_blerc_run(dc_m2c_env *e) {
  /* The scratchpad at the region's start, 64 KiB aligned like the port's: setup
   * ends each chain with its address as an (empty) refe tag. */
  enum { SPAD = 0x0000, GLOBALS = 0x4100, WEIGHTS = 0x4200, HEADER = 0x4300, OUT = 0x5000, SRC = 0x6000,
         DEST = 0x8000, GSF = 0xc000 };
  static const int16_t weight[3] = {100, 0, -50};
  static const uint8_t header[5] = {40, 0, 1, 1, 1};
  uint32_t i, sum = 0, chain;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*blerc-globals*", dc_m2c_ptr(e, GLOBALS));
  e->set_symbol(e, "*gsf-buffer*", dc_m2c_ptr(e, GSF));
  e->set_symbol(e, "*stats-blerc*", e->s7);
  e->set_symbol(e, "flush-cache", dc_m2c_goal(e, 0, dc_m2c_flush_cache));
  memcpy(e->region + WEIGHTS, weight, sizeof weight);
  for (i = 0; i < 0x1800; ++i) dc_m2c_b(e, SRC + i, (uint8_t)(i * 29u + (i >> 7)));
  for (i = 0; i < 0x2000; i += 4) dc_m2c_u(e, DEST + i, 0x00400040u + i);
  /* As setup-blerc-chains: the chain starts 16 bytes into the buffer, then a
   * next tag at the start points past its end. */
  dc_m2c_u(e, GLOBALS + 0, dc_m2c_ptr(e, OUT + 16));
  memcpy(e->region + HEADER, header, sizeof header);
  {
    const uint64_t args[8] = {3, dc_m2c_ptr(e, WEIGHTS), dc_m2c_ptr(e, OUT + 16), dc_m2c_ptr(e, SRC),
                              dc_m2c_ptr(e, HEADER), dc_m2c_ptr(e, DEST), 0, 0};
    chain = (uint32_t)e->call(e, "setup-blerc-chains-for-one-fragment", args);
  }
  dc_m2c_u(e, OUT + 0, 0x20000000u); dc_m2c_u(e, OUT + 4, chain & 0x7fffffffu);
  dc_m2c_call0(e, "blerc-execute");
  for (i = 0; i < 0x2000; i += 4) {
    uint32_t w;
    memcpy(&w, e->region + DEST + i, 4);
    sum += w != 0x00400040u + i;
  }
  return sum;  /* destination words blended */
}

/* ---- collide_edge_grab: find-best-grab! (method 18 of collide-edge-work), Jak's
 * ledge-grab search. One hold item on a long edge is split forward and back by
 * the split distances at work +168; each piece goes to should-add-to-list?
 * (method 17, a stand-in that sets its edge and a rating, and turns one down) and
 * the built add-to-list!, which keeps the list sorted by rating. A stand-in
 * check-grab-for-collisions (method 19) takes the fourth candidate, the last
 * piece the search would offer. Both GOAL
 * methods are reached through stand-in type objects. ---- */
static uint32_t dc_m2c_grab_calls[2];
static uint64_t dc_m2c_should_add(dc_m2c_env *e, const uint64_t *a) {
  static const uint32_t rating[8] = {0x3f000000u, 0x3e800000u, 0x3f400000u, 0x3e000000u,
                                     0x3f200000u, 0x3d800000u, 0x3f600000u, 0x3ec00000u};
  uint32_t item = dc_m2c_off(e, a[1]), n = dc_m2c_grab_calls[0]++;
  dc_m2c_log(e, 0x17 << 24 | item);
  if (n == 2) return e->s7;  /* turned down */
  dc_m2c_u(e, item + 12, (uint32_t)a[2]);
  dc_m2c_u(e, item + 4, rating[n % 8]);
  return e->s7 + 8;
}
static uint64_t dc_m2c_check_grab(dc_m2c_env *e, const uint64_t *a) {
  uint32_t item = dc_m2c_off(e, a[1]), n = dc_m2c_grab_calls[1]++;
  dc_m2c_log(e, 0x19 << 24 | item);
  return n == 3 ? e->s7 + 8 : e->s7;
}
static inline uint32_t dc_m2c_find_best_grab(dc_m2c_env *e) {
  enum { WORK = 0x0100, LIST = 0x0400, INFO = 0x0d00, EDGE = 0x1000, VERTS = 0x1040, WORK_TYPE = 0x1100,
         LIST_TYPE = 0x1200 };
  uint64_t r;
  memset(dc_m2c_grab_calls, 0, sizeof dc_m2c_grab_calls);
  e->set_symbol(e, "collide-edge-work", dc_m2c_ptr(e, WORK_TYPE));
  e->set_symbol(e, "collide-edge-hold-list", dc_m2c_ptr(e, LIST_TYPE));
  dc_m2c_u(e, WORK_TYPE + 84, dc_m2c_goal(e, 0, dc_m2c_should_add));
  dc_m2c_u(e, WORK_TYPE + 92, dc_m2c_goal(e, 1, dc_m2c_check_grab));
  dc_m2c_u(e, LIST_TYPE + 56, e->mips2c_function(e, "(method 10 collide-edge-hold-list)"));
  dc_m2c_f(e, WORK + 168, 3000.0f); dc_m2c_f(e, WORK + 172, 6000.0f);  /* split distances */
  /* The edge: from the origin 20000 along x. */
  dc_m2c_vec(e, VERTS, 0.0f, 0.0f, 0.0f, 1.0f); dc_m2c_vec(e, VERTS + 16, 20000.0f, 0.0f, 0.0f, 1.0f);
  dc_m2c_u(e, EDGE + 8, dc_m2c_ptr(e, VERTS)); dc_m2c_u(e, EDGE + 12, dc_m2c_ptr(e, VERTS + 16));
  dc_m2c_vec(e, EDGE + 16, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, EDGE + 32, 1.0f, 0.0f, 0.0f, 0.0f);
  /* The hold list: one item at 5000 along the edge, unsplit. */
  dc_m2c_u(e, LIST + 0, 1); dc_m2c_u(e, LIST + 8, dc_m2c_ptr(e, LIST + 16));
  dc_m2c_u(e, LIST + 16 + 0, e->s7); dc_m2c_f(e, LIST + 16 + 4, 0.375f);
  dc_m2c_u(e, LIST + 16 + 12, dc_m2c_ptr(e, EDGE));
  dc_m2c_vec(e, LIST + 16 + 16, 5000.0f, 0.0f, 0.0f, 1.0f);
  r = dc_m2c_call(e, "(method 18 collide-edge-work)", dc_m2c_ptr(e, WORK), dc_m2c_ptr(e, LIST),
                  dc_m2c_ptr(e, INFO), 0, 0, 0);
  dc_m2c_u(e, LIST_TYPE + 56, 0);  /* the native entry's address differs between the sides */
  return ((uint32_t)r == e->s7 + 8) << 16 | dc_m2c_grab_calls[0] << 8 | dc_m2c_grab_calls[1];
}

/* ---- collide_probe: collide-probe-instance-tie over a TIE tree in the box
 * *collide-work* describes. Five root nodes (two far); the first holds two more
 * nodes, the others instance arrays: five instances (one far) or one. Each
 * instance's prototype has five collide frags (two far) in the instance's frame (a
 * 4.12 matrix and a translation). The near frags of the near instances land in the
 * collide list. Five of each runs the four-at-a-time loops twice. ---- */
static inline void dc_m2c_tie_node(dc_m2c_env *e, uint32_t at, float x, uint32_t child, uint8_t count,
                                   uint8_t nodes) {
  dc_m2c_vec(e, at + 12, x, 0.0f, 0.0f, 4000.0f);
  dc_m2c_u(e, at + 4, child); dc_m2c_b(e, at + 2, count); dc_m2c_b(e, at + 3, nodes);
}
static inline uint32_t dc_m2c_probe_instance_tie(dc_m2c_env *e) {
  enum { WORK = 0x0100, STACK = 0x0200, NODES = 0x0404, NODES2 = 0x04c4, INSTS = 0x0504, INSTS2 = 0x0684,
         BUCKET = 0x0704, FRAGS = 0x0804, MESH = 0x0904, LIST = 0x1000 };
  static const int16_t rows[16] = {4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 10, 20, -30, 0};
  static const float node_x[5] = {0.0f, 50000.0f, 1000.0f, -1000.0f, -60000.0f};
  static const float inst_x[6] = {0.0f, 40000.0f, 1000.0f, 2000.0f, -1500.0f, 500.0f};
  static const float frag_x[5] = {200.0f, 30000.0f, -300.0f, -30000.0f, 400.0f};
  uint32_t i, count;
  e->set_symbol(e, "*collide-work*", dc_m2c_ptr(e, WORK));
  e->set_symbol(e, "*collide-probe-stack*", dc_m2c_ptr(e, STACK));
  /* The probe's box and frame (identity). */
  dc_m2c_u(e, WORK + 16, (uint32_t)-10000); dc_m2c_u(e, WORK + 20, (uint32_t)-10000); dc_m2c_u(e, WORK + 24, (uint32_t)-10000);
  dc_m2c_u(e, WORK + 32, 10000); dc_m2c_u(e, WORK + 36, 10000); dc_m2c_u(e, WORK + 40, 10000);
  /* All four lanes are compared; a frag's transformed w carries its instance's
   * radius, so the box's w is wide. */
  dc_m2c_u(e, WORK + 28, (uint32_t)-1000000); dc_m2c_u(e, WORK + 44, 1000000);
  dc_m2c_vec(e, WORK + 48, 1.0f, 0.0f, 0.0f, 0.0f); dc_m2c_vec(e, WORK + 64, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, WORK + 80, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, WORK + 96, 0.0f, 0.0f, 0.0f, 1.0f);
  for (i = 0; i < 5; ++i)
    dc_m2c_tie_node(e, NODES + 32 * i, node_x[i], dc_m2c_ptr(e, i == 0 ? NODES2 : i == 3 ? INSTS2 : INSTS),
                    i == 0 ? 2 : i == 3 ? 1 : 5, i == 0);
  dc_m2c_tie_node(e, NODES2, 500.0f, dc_m2c_ptr(e, INSTS), 5, 0);
  dc_m2c_tie_node(e, NODES2 + 32, -500.0f, dc_m2c_ptr(e, INSTS2), 1, 0);
  for (i = 0; i < 6; ++i) {
    uint32_t inst = i < 5 ? INSTS + 64 * i : INSTS2;
    uint16_t scale = 4096, flags = 0;
    dc_m2c_vec(e, inst + 12, inst_x[i], 100.0f, -500.0f, 2000.0f);
    dc_m2c_u(e, inst + 8, dc_m2c_ptr(e, BUCKET));
    memcpy(e->region + inst + 28, rows, sizeof rows);
    memcpy(e->region + inst + 34, &scale, 2);  /* overlays row 0's w: 1.0 in 4.12 */
    memcpy(e->region + inst + 42, &flags, 2);
  }
  /* The prototype's collide frags. */
  dc_m2c_u(e, BUCKET + 136, dc_m2c_ptr(e, FRAGS));
  {
    uint16_t n = 5;
    memcpy(e->region + FRAGS + 2, &n, 2);
  }
  for (i = 0; i < 5; ++i) {
    dc_m2c_vec(e, FRAGS + 32 + 32 * i + 12, frag_x[i], 0.0f, 100.0f, 500.0f);
    dc_m2c_u(e, FRAGS + 32 + 32 * i + 4, dc_m2c_ptr(e, MESH + 16 * i));
  }
  dc_m2c_call(e, "collide-probe-instance-tie", dc_m2c_ptr(e, NODES), 5, dc_m2c_ptr(e, LIST), 1, 0, 0);
  memcpy(&count, e->region + LIST, 4);
  return count;  /* collide-list entries */
}

/* ---- joint: calc-animation-from-spr and its decompressors over two of Jak's
 * animations from the user's own GAME.CGO (anim_case_data.py writes them to
 * build/mips2c/anim_case.h, which is not committed; without it this case is left
 * out). The scratchpad is set up as flatten-joint-control-to-spr and
 * make-joint-jump-tables would: the jump tables at 1632, the uploads at 1824 and
 * their count at 2400. Each animation runs alone and as an interpolated pair, then
 * two are blended. ---- */
#if defined(__has_include)
#if __has_include("build/mips2c/anim_case.h")
#include "build/mips2c/anim_case.h"
#endif
#endif
#ifdef DC_M2C_ANIM_COUNT
static inline uint32_t dc_m2c_crc32(const unsigned char *p, uint32_t n);
static inline uint32_t dc_m2c_calc_animation(dc_m2c_env *e) {
  enum { SPAD = 0x0000, RESULTS = 0x3f00, FIXED = 0x4000, FRAMES = 0x6000, OUT = 0xa000, OUT_BYTES = 0x1800, JOINTS = 81 };
  static const uint32_t jump[48] = {
    0x3ee6b6f9u, 0x3ee6b6ffu, 0x3ee6b6e2u, 0x3ee6b6e4u, 0x3ee6b6e5u, 0x3ee6b6ebu, 0x3ee6b6eeu, 0x3ee6b6f6u,
    0x3ee6b6fcu, 0x3ee6b6ffu, 0x3ee6b6e3u, 0x3ee6b6e4u, 0x3ee6b6e8u, 0x3ee6b6ebu, 0x3ee6b6efu, 0x3ee6b6f6u,
    0x07b2191bu, 0x07b21914u, 0x07b21916u, 0x07b21913u, 0x07b21909u, 0x07b21908u, 0x07b2190au, 0x07b21907u,
    0x07b2191bu, 0x07b21917u, 0x07b21916u, 0x07b2190cu, 0x07b21909u, 0x07b2190bu, 0x07b2190au, 0x07b21900u,
    0x4d7666dfu, 0x4d7666d0u, 0x4d7666d2u, 0x4d7666d7u, 0x4d7666cdu, 0x4d7666ccu, 0x4d7666ceu, 0x4d7666c3u,
    0x4d7666dfu, 0x4d7666d3u, 0x4d7666d2u, 0x4d7666c8u, 0x4d7666cdu, 0x4d7666cfu, 0x4d7666ceu, 0x4d7666c4u};
  uint32_t fixed_at[DC_M2C_ANIM_COUNT], frames_at[DC_M2C_ANIM_COUNT], at_fixed = FIXED, at_frames = FRAMES;
  uint32_t i, run, sum = 0;
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  for (i = 0; i < DC_M2C_ANIM_COUNT; ++i) {
    fixed_at[i] = at_fixed; frames_at[i] = at_frames;
    memcpy(e->region + at_fixed, dc_m2c_anims[i].fixed, 16 * dc_m2c_anims[i].fixed_qwc);
    memcpy(e->region + at_frames, dc_m2c_anims[i].frame, 16 * dc_m2c_anims[i].frame_qwc * dc_m2c_anims[i].frames);
    at_fixed += 16 * dc_m2c_anims[i].fixed_qwc;
    at_frames += 16 * dc_m2c_anims[i].frame_qwc * dc_m2c_anims[i].frames;
  }
  for (i = 0; i < 48; ++i) dc_m2c_u(e, SPAD + 1632 + 4 * i, jump[i]);
  /* Each animation alone at frame 0, then as a pair of frames 0 and 1 (interp
   * 0.5), then the first two blended half and half. Each run's output is hashed
   * into the region, since the next one overwrites it. */
  for (run = 0; run < 2 * DC_M2C_ANIM_COUNT + 1; ++run) {
    const unsigned blend = run == 2 * DC_M2C_ANIM_COUNT, count = blend ? 2 : 1;
    for (i = 0; i < count; ++i) {
      const unsigned a = blend ? i : run / 2;
      const float interp = !blend && (run & 1) ? 0.5f : 0.0f;
      const uint32_t rec = SPAD + 1824 + 24 * i, qwc = dc_m2c_anims[a].frame_qwc;
      dc_m2c_u(e, rec + 0, dc_m2c_ptr(e, fixed_at[a]));
      dc_m2c_u(e, rec + 4, dc_m2c_anims[a].fixed_qwc);
      dc_m2c_u(e, rec + 8, dc_m2c_ptr(e, frames_at[a]));
      dc_m2c_u(e, rec + 12, interp == 0.0f ? qwc : 2 * qwc);
      dc_m2c_f(e, rec + 16, blend ? 0.5f : 1.0f);
      dc_m2c_f(e, rec + 20, interp);
    }
    dc_m2c_u(e, SPAD + 2400, count);
    memset(e->region + OUT, 0, OUT_BYTES);
    dc_m2c_call(e, "calc-animation-from-spr", dc_m2c_ptr(e, OUT), JOINTS, 0, 0, 0, 0);
    dc_m2c_u(e, RESULTS + 4 * run, dc_m2c_crc32(e->region + OUT, OUT_BYTES));
    for (i = 0; i < OUT_BYTES; i += 4) {
      uint32_t w;
      memcpy(&w, e->region + OUT + i, 4);
      sum += w != 0;
    }
  }
  return sum;  /* output words written, over all runs */
}
#endif

/* ---- collide_cache: the background fill over three of village1's own collide-frag
 * meshes from the user's disc (mesh_case_data.py writes them to
 * build/mips2c/mesh_case.h, which is not committed; without it this case is left
 * out). Each mesh is uploaded and unpacked as fill-from-background does, then
 * turned into cache tris by method 26 (a box around everything), method 28 (a box
 * cutting through the middle of the first mesh) and method 27 (line-sphere,
 * through method 29). After the first fill, puyp-mesh (method 30) drops a probe
 * onto the first tri. ---- */
#if defined(__has_include)
#if __has_include("build/mips2c/mesh_case.h")
#include "build/mips2c/mesh_case.h"
#endif
#endif
#ifdef DC_M2C_MESH_COUNT
static inline uint32_t dc_m2c_real_meshes(dc_m2c_env *e) {
  enum { TYPE = 0x0100, MESHES = 0x0204, WORK = 0x0400, PRIM = 0x0500, PUYP = 0x0600, SPAD = 0x8000, PACKED = 0xa000,
         PATS = 0xb800 };
  static const char *const method[3] = {"(method 26 collide-cache)", "(method 28 collide-cache)",
                                        "(method 27 collide-cache)"};
  uint32_t i, pass, at = PACKED, tris, sum = 0, low_x = 0xffff, high_x = 0;
  for (i = 0; i < dc_m2c_meshes[0].verts; ++i) {
    uint16_t x;
    memcpy(&x, dc_m2c_meshes[0].packed + 6 * i, 2);
    low_x = x < low_x ? x : low_x; high_x = x > high_x ? x : high_x;
  }
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*collide-cache-max-tris*", 461);
  e->set_symbol(e, "*already-printed-exeeded-max-cache-tris*", e->s7 + 8);
  e->set_symbol(e, "*collide-work*", dc_m2c_ptr(e, WORK));
  dc_m2c_u(e, DC_M2C_CC_CACHE - 4, dc_m2c_ptr(e, TYPE));
  dc_m2c_u(e, TYPE + 132, e->mips2c_function(e, "(method 29 collide-cache)"));
  memcpy(e->region + PATS, dc_m2c_mesh_pats, sizeof dc_m2c_mesh_pats);
  for (i = 0; i < DC_M2C_MESH_COUNT; ++i) {
    const uint32_t mesh = MESHES + 32 * i;
    memcpy(e->region + at, dc_m2c_meshes[i].packed, 16u * dc_m2c_meshes[i].total_qwc);
    dc_m2c_u(e, mesh + 0, dc_m2c_ptr(e, at)); dc_m2c_u(e, mesh + 4, dc_m2c_ptr(e, PATS));
    dc_m2c_u(e, mesh + 8, (uint32_t)dc_m2c_meshes[i].polys << 16 | dc_m2c_meshes[i].strip_len);
    dc_m2c_u(e, mesh + 12, (uint32_t)dc_m2c_meshes[i].base[0]); dc_m2c_u(e, mesh + 16, (uint32_t)dc_m2c_meshes[i].base[1]);
    dc_m2c_u(e, mesh + 20, (uint32_t)dc_m2c_meshes[i].base[2]);
    dc_m2c_u(e, mesh + 24, (uint32_t)dc_m2c_meshes[i].total_qwc << 16 | (uint32_t)dc_m2c_meshes[i].vertex_qwc << 8 |
                               dc_m2c_meshes[i].verts);
    at += 16u * dc_m2c_meshes[i].total_qwc;
  }
  /* *collide-work* for line-sphere: an identity frame and a box around everything. */
  dc_m2c_u(e, WORK + 16, 0xc0000000u); dc_m2c_u(e, WORK + 20, 0xc0000000u); dc_m2c_u(e, WORK + 24, 0xc0000000u);
  dc_m2c_u(e, WORK + 32, 0x3fffffffu); dc_m2c_u(e, WORK + 36, 0x3fffffffu); dc_m2c_u(e, WORK + 40, 0x3fffffffu);
  dc_m2c_vec(e, WORK + 48, 1.0f, 0.0f, 0.0f, 0.0f); dc_m2c_vec(e, WORK + 64, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_m2c_vec(e, WORK + 80, 0.0f, 0.0f, 1.0f, 0.0f); dc_m2c_vec(e, WORK + 96, 0.0f, 0.0f, 0.0f, 1.0f);
  for (pass = 0; pass < 3; ++pass) {
    /* The cache's box: around everything, or (pass 1) ending halfway across the first
     * mesh (a vertex is 16 units a step from its mesh's base). */
    const uint32_t max_x = pass == 1 ? (uint32_t)(dc_m2c_meshes[0].base[0] + 8 * (int32_t)(low_x + high_x)) : 0x3fffffffu;
    dc_m2c_u(e, DC_M2C_CC_CACHE + 0, 0); dc_m2c_u(e, DC_M2C_CC_CACHE + 8, 0);
    dc_m2c_u(e, DC_M2C_CC_CACHE + 60, 0xc0000000u); dc_m2c_u(e, DC_M2C_CC_CACHE + 64, 0xc0000000u);
    dc_m2c_u(e, DC_M2C_CC_CACHE + 68, 0xc0000000u);
    dc_m2c_u(e, DC_M2C_CC_CACHE + 76, max_x); dc_m2c_u(e, DC_M2C_CC_CACHE + 80, 0x3fffffffu);
    dc_m2c_u(e, DC_M2C_CC_CACHE + 84, 0x3fffffffu);
    for (i = 0; i < DC_M2C_MESH_COUNT; ++i) {
      const uint32_t mesh = MESHES + 32 * i;
      uint32_t packed;
      memcpy(&packed, e->region + mesh, 4);
      dc_m2c_call(e, "__pc-upload-collide-frag", packed, dc_m2c_meshes[i].vertex_qwc, dc_m2c_meshes[i].verts, 0, 0, 0);
      dc_m2c_call(e, "(method 32 collide-cache)", dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, mesh), e->s7, 0, 0, 0);
      dc_m2c_call(e, method[pass], dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, mesh), 0, 0, 0, 0);
      memcpy(&tris, e->region + DC_M2C_CC_CACHE, 4);
      dc_m2c_u(e, DC_M2C_LOG + 4 + 4 * (3 * pass + i), tris);
    }
    sum = sum << 10 | tris;
    if (pass == 0) {
      /* puyp-mesh (method 30): a probe dropped from 4096 above the first tri's
       * centroid onto all of them. */
      float v[3][3], c[3];
      unsigned k, n;
      for (k = 0; k < 3; ++k)
        for (n = 0; n < 3; ++n) memcpy(&v[k][n], e->region + DC_M2C_CC_TRIS + 16 * k + 4 * n, 4);
      for (n = 0; n < 3; ++n) c[n] = (v[0][n] + v[1][n] + v[2][n]) / 3.0f;
      dc_m2c_u(e, PRIM + 32, dc_m2c_ptr(e, DC_M2C_CC_CACHE)); dc_m2c_u(e, PRIM + 40, tris << 16);
      dc_m2c_f(e, PUYP + 0, 1.0f); dc_m2c_u(e, PUYP + 4, 0); dc_m2c_u(e, PUYP + 8, dc_m2c_ptr(e, PUYP + 48));
      dc_m2c_vec(e, PUYP + 16, c[0], c[1] + 4096.0f, c[2], 1.0f); dc_m2c_vec(e, PUYP + 32, 0.0f, -8192.0f, 0.0f, 0.0f);
      dc_m2c_call(e, "(method 30 collide-cache)", dc_m2c_ptr(e, DC_M2C_CC_CACHE), dc_m2c_ptr(e, PUYP),
                  dc_m2c_ptr(e, PRIM), 0, 0, 0);
    }
    if (pass < 2) memset(e->region + DC_M2C_CC_TRIS, 0, 64u * tris);  /* the next pass starts empty */
  }
  dc_m2c_u(e, TYPE + 132, 0);  /* the native entry's address differs between the sides */
  return sum;  /* tris in the cache after each pass, 10 bits apiece */
}
#endif

/* ---- shadow: shadow-execute over Jak's own shadow geometry from the user's
 * GAME.CGO (anim_case_data.py, with the animations): 35 joints, 118 vertices, 153
 * single and 14 double tris. The packet is draw-bones-shadow's: a cnt tag over
 * the settings, a ref to the geometry, a ref to the joint matrices, an end tag.
 * The settings are the synthetic case's, in game units (4096 a meter). ---- */
#ifdef DC_M2C_JAK_SHADOW_JOINTS
static inline uint32_t dc_m2c_jak_shadow_case(dc_m2c_env *e) {
  enum { CAMERA = 0x0004, SDATA = 0x0600, PACKET = 0x1000, GEO = 0x1100, MTX = 0x2100, SPAD = 0x4000,
         GSF = 0x8000, OUT = 0xa000 };
  uint32_t i, k, state = 0x5ade6u;
  uint64_t end;
  e->set_symbol(e, "*math-camera*", dc_m2c_ptr(e, CAMERA));
  e->set_symbol(e, "*fake-scratchpad-data*", dc_m2c_ptr(e, SPAD));
  e->set_symbol(e, "*gsf-buffer*", dc_m2c_ptr(e, GSF));
  e->set_symbol(e, "flush-cache", dc_m2c_goal(e, 0, dc_m2c_flush_cache));
  e->set_symbol(e, "camera-pos", dc_m2c_goal(e, 1, dc_m2c_camera_pos));
  e->set_symbol(e, "*shadow-data*", dc_m2c_ptr(e, SDATA));
  for (i = 0; i < 64; ++i) dc_m2c_u(e, SDATA + 4 * i, 0x10000000u * (i & 7) + 0x101u * i);
  dc_m2c_f(e, CAMERA + 0, 1.0f);
  for (i = 0; i < 16; ++i) dc_m2c_f(e, CAMERA + 364 + 4 * i, (i % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.0625f);
  dc_m2c_vec(e, DC_M2C_SHADOW_CAMPOS, 0.0f, 40960.0f, 81920.0f, 1.0f);
  /* The packet. */
  dc_m2c_u(e, PACKET + 0, 1u << 28 | 5);  /* cnt over the settings; +12 is the next packet (none) */
  dc_m2c_vec(e, PACKET + 16, 0.0f, 4096.0f, 0.0f, 0.0f); dc_m2c_u(e, PACKET + 28, 1 | 8);  /* camera check, scissor top */
  dc_m2c_vec(e, PACKET + 32, 0.25f, -1.0f, 0.125f, 32768.0f);
  dc_m2c_vec(e, PACKET + 48, 0.0f, 1.0f, 0.0f, -2048.0f);
  dc_m2c_vec(e, PACKET + 64, 0.0f, 1.0f, 0.0f, 16384.0f);
  dc_m2c_vec(e, PACKET + 80, 0.0f, -1.0f, 0.0f, 24576.0f);
  dc_m2c_u(e, PACKET + 96, 3u << 28 | (uint32_t)(sizeof dc_m2c_jak_shadow / 16)); dc_m2c_u(e, PACKET + 100, dc_m2c_ptr(e, GEO));
  dc_m2c_u(e, PACKET + 112, 3u << 28 | 8 * DC_M2C_JAK_SHADOW_JOINTS); dc_m2c_u(e, PACKET + 116, dc_m2c_ptr(e, MTX));
  dc_m2c_u(e, PACKET + 128, 7u << 28);
  memcpy(e->region + GEO, dc_m2c_jak_shadow, sizeof dc_m2c_jak_shadow);
  /* Joint matrices, 128 bytes a joint: near-identity rows, raised 1.5 m. */
  for (i = 0; i < DC_M2C_JAK_SHADOW_JOINTS; ++i) {
    for (k = 0; k < 12; ++k) dc_m2c_f(e, MTX + 128 * i + 4 * k, (k % 5 == 0) ? 1.0f : dc_m2c_rand(&state) * 0.03125f);
    dc_m2c_vec(e, MTX + 128 * i + 48, 0.0f, 6144.0f, 0.0f, 1.0f);
  }
  end = dc_m2c_call(e, "shadow-execute", dc_m2c_ptr(e, PACKET), dc_m2c_ptr(e, OUT), 0, 0, 0, 0);
  return dc_m2c_off(e, end) - OUT;  /* packet bytes */
}
#endif

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
  {"shadow-execute", dc_m2c_shadow_execute},
  {"sparticle", dc_m2c_sparticle},
  {"sparticle_launcher", dc_m2c_particle_adgif},
  {"sp-launch-particles-var", dc_m2c_launch_particles},
  {"ripple-create-wave-table", dc_m2c_ripple_wave_table},
  {"bones-mtx-calc", dc_m2c_bones_mtx_calc},
  {"collide_cache", dc_m2c_collide_cache_query},
  {"collide_cache_fill", dc_m2c_collide_cache_fill},
  {"collide_cache_prims", dc_m2c_collide_cache_prims},
  {"generic-prepare-dma", dc_m2c_prepare_dma},
  {"sky-quad-boundary", dc_m2c_sky_quad_boundary},
  {"collide-mesh-tests", dc_m2c_collide_mesh_tests},
  {"generic-envmap-interp", dc_m2c_generic_envmap_interp},
  {"shadow-double", dc_m2c_shadow_double},
  {"sp-launch-3d", dc_m2c_launch_3d},
  {"collide-do-primitives", dc_m2c_collide_primitives},
  {"ocean-generate-verts", dc_m2c_ocean_verts},
  {"blerc-execute", dc_m2c_blerc_run},
  {"find-best-grab", dc_m2c_find_best_grab},
  {"collide-probe-instance-tie", dc_m2c_probe_instance_tie},
#ifdef DC_M2C_ANIM_COUNT
  {"calc-animation-from-spr", dc_m2c_calc_animation},
#endif
#ifdef DC_M2C_MESH_COUNT
  {"village1-collide-meshes", dc_m2c_real_meshes},
#endif
#ifdef DC_M2C_JAK_SHADOW_JOINTS
  {"jak-shadow", dc_m2c_jak_shadow_case},
#endif
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
  uint32_t at, w, k;
  memset(e->region, 0, DC_M2C_REGION);
  memset(dc_m2c_goal_address, 0, sizeof dc_m2c_goal_address);
  *result = dc_m2c_cases[i].run(e);
  for (at = 0; at < DC_M2C_REGION; at += 4) {
    memcpy(&w, e->region + at, 4);
    for (k = 0; k < DC_M2C_GOAL_SLOTS && !(w && w == dc_m2c_goal_address[k]); ++k) {}
    if (k < DC_M2C_GOAL_SLOTS) w = DC_M2C_GOAL_MARK + k;
    else if (w == e->s7) w = DC_M2C_FALSE_MARK;
    else if (w == e->s7 + 8) w = DC_M2C_TRUE_MARK;
    else if (w >= e->base && w - e->base < DC_M2C_REGION) w -= e->base;
    else continue;
    memcpy(e->region + at, &w, 4);
  }
  return dc_m2c_crc32(e->region, DC_M2C_REGION);
}
#endif
