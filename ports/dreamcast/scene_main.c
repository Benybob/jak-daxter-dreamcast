#include <kos.h>
#include <dc/biosfont.h>
#include <dc/maple/controller.h>
#include <math.h>
#include <malloc.h>
#include <stdlib.h>
#include "scene_io.h"
#include "scene_telemetry.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

volatile dc_scene_telemetry dc_telemetry = { .magic = DC_TELEMETRY_MAGIC, .version = 1 };

static void record_error(const char *message) {
  unsigned i;
  for (i = 0; i < sizeof(dc_telemetry.error)-1 && message[i]; ++i)
    dc_telemetry.error[i] = message[i];
  dc_telemetry.error[i] = 0;
  dc_telemetry.state = DC_ERROR;
}

static pvr_poly_hdr_t geometry_header, label_header;
static pvr_ptr_t label_texture;

static int make_label(int fixture) {
  pvr_poly_cxt_t context;
  uint16_t *pixels = calloc(512 * 64, 2);
  if (!pixels) return -1;
  bfont_draw_str_ex(pixels, 512, 0xffff, 0x8000, 16, true,
                   fixture ? "SYNTHETIC GEOMETRY / Dreamcast" : "EXTERNAL GEOMETRY / Dreamcast");
  bfont_draw_str_ex(pixels + 512 * 30, 512, 0xffff, 0x8000, 16, true,
                   "Stick: orbit  L/R: zoom  START: exit");
  label_texture = pvr_mem_malloc(512 * 64 * 2);
  if (!label_texture) { free(pixels); return -1; }
  pvr_txr_load(pixels, label_texture, 512 * 64 * 2);
  free(pixels);
  pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                   PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                   512, 64, label_texture, PVR_FILTER_NONE);
  context.gen.culling = PVR_CULLING_NONE;
  pvr_poly_compile(&label_header, &context);
  return 0;
}

static void draw_label(void) {
  unsigned i;
  const float x[4] = {16, 528, 16, 528}, y[4] = {12, 12, 76, 76};
  pvr_vertex_t v = {0};
  pvr_prim(&label_header, sizeof(label_header));
  for (i = 0; i < 4; ++i) {
    v.flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    v.x = x[i]; v.y = y[i]; v.z = 100.0f;
    v.u = (i & 1) ? 1.0f : 0.0f; v.v = i >= 2 ? 1.0f : 0.0f;
    v.argb = 0xffffffffu;
    pvr_prim(&v, sizeof(v));
  }
}

static void draw_scene(const dc_scene *scene, float yaw, float pitch, float distance) {
  uint32_t i;
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  pvr_vertex_t out = {0};
  pvr_prim(&geometry_header, sizeof(geometry_header));
  for (i = 0; i < scene->count; ++i) {
    const dc_scene_vertex *v = &scene->vertices[i];
    float x = cy*v->x + sy*v->z, z = -sy*v->x + cy*v->z;
    float y = cp*v->y - sp*z;
    z = sp*v->y + cp*z + distance;
    /* Unit-sphere input and distance >= 1.5 guarantee z > 0.49.
     * This orbit viewer therefore never crosses the near plane. */
    out.x = 320.0f + 390.0f*x/z;
    out.y = 260.0f - 390.0f*y/z;
    out.z = 1.0f/z;
    out.flags = i % 3 == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    out.argb = v->argb;
    pvr_prim(&out, sizeof(out));
  }
}

int main(void) {
  dc_scene scene;
  const char *path = "/cd/SCENE.DCM", *error;
  FILE *file = fopen(path, "rb");
  int fixture = 0;
  pvr_poly_cxt_t context;
  pvr_init_params_t params = { .opb_sizes = {PVR_BINSIZE_32, 0, 0, 0, 0},
                              .vertex_buf_size = 1024 * 1024,
                              .opb_overflow_count = 3 };
  float yaw = 0.0f, pitch = 0.5f, distance = 2.6f;
  uint64_t previous;
  unsigned frames = 0;
  dc_telemetry.state = DC_BOOT;
  dc_telemetry.source = 1;
  if (!file) { path = "/rd/scene.dcm"; dc_telemetry.source = 2; file = fopen(path, "rb"); }
  if (!file) { fixture = 1; path = "/rd/fixture.dcm"; dc_telemetry.source = 3; file = fopen(path, "rb"); }
  if (!file) { record_error("cannot open scene"); printf("SCENE_FAIL cannot open scene\n"); return 1; }
  error = dc_scene_load(file, &scene);
  fclose(file);
  if (error) { record_error(error); printf("SCENE_FAIL %s\n", error); return 1; }
  vid_set_mode(DM_640x480, PM_RGB565);
  if (pvr_init(&params) < 0) { record_error("PVR initialization failed"); dc_scene_free(&scene); return 1; }
  pvr_set_bg_color(0.05f, 0.10f, 0.16f);
  pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
  context.gen.culling = PVR_CULLING_NONE;
  context.gen.shading = PVR_SHADE_GOURAUD;
  pvr_poly_compile(&geometry_header, &context);
  if (make_label(fixture) < 0) { record_error("label allocation failed"); pvr_shutdown(); dc_scene_free(&scene); return 1; }
  printf("SCENE_READY source=%s triangles=%lu geometry_bytes=%lu vram_free=%lu\n",
         path, (unsigned long)(scene.count/3),
         (unsigned long)(scene.count*sizeof(dc_scene_vertex)), (unsigned long)pvr_mem_available());
  {
    struct mallinfo memory = mallinfo();
    dc_telemetry.triangles = scene.count/3;
    dc_telemetry.geometry_bytes = scene.count*sizeof(dc_scene_vertex);
    dc_telemetry.system_ram = _arch_mem_top - 0x8c000000u;
    dc_telemetry.allocator_live = memory.uordblks;
    dc_telemetry.allocator_arena = memory.arena;
    dc_telemetry.vram_free = pvr_mem_available();
    dc_telemetry.state = DC_READY;
    printf("SCENE_MEMORY allocator_live=%d allocator_arena=%d system_ram=%lu\n",
           memory.uordblks, memory.arena, (unsigned long)(_arch_mem_top - 0x8c000000u));
  }
  previous = timer_us_gettime64();
  while (1) {
    uint64_t now = timer_us_gettime64();
    float dt = (float)(now - previous) / 1000000.0f;
    maple_device_t *device = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = device ? maple_dev_status(device) : NULL;
    dc_telemetry.frame_us = (uint32_t)(now - previous);
    previous = now;
    if (dt > 0.05f) dt = 0.05f;
    yaw += 0.25f * dt;
    if (state) {
      if (state->buttons & CONT_START) break;
      if (abs(state->joyx) > 12) yaw += state->joyx/64.0f * dt;
      if (abs(state->joyy) > 12) pitch += state->joyy/128.0f * dt;
      distance += ((float)state->ltrig - state->rtrig)/128.0f * dt;
    }
    if (pitch > 1.3f) pitch = 1.3f;
    if (pitch < -1.3f) pitch = -1.3f;
    if (distance < 1.5f) distance = 1.5f;
    if (distance > 6.0f) distance = 6.0f;
    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    draw_scene(&scene, yaw, pitch, distance);
    draw_label();
    pvr_list_finish();
    pvr_scene_finish();
    dc_telemetry.yaw = yaw;
    dc_telemetry.pitch = pitch;
    dc_telemetry.distance = distance;
    dc_telemetry.frames = frames+1;
    if (++frames % 120 == 0) {
      pvr_stats_t stats;
      pvr_get_stats(&stats);
      dc_telemetry.pvr_fps = stats.frame_rate;
      printf("SCENE_FRAME frames=%u pvr_fps=%.2f yaw=%.3f pitch=%.3f distance=%.3f\n",
             frames, (double)stats.frame_rate, (double)yaw, (double)pitch, (double)distance);
    }
  }
  pvr_wait_ready();
  pvr_mem_free(label_texture);
  pvr_shutdown();
  dc_scene_free(&scene);
  dc_telemetry.state = DC_EXIT;
  printf("SCENE_EXIT frames=%u\n", frames);
  return 0;
}
