/* AI-assisted target diagnostics, read through emulator memory or a debugger. */
#ifndef JAK_DC_SCENE_TELEMETRY_H
#define JAK_DC_SCENE_TELEMETRY_H
#include <stdint.h>
#include <stddef.h>

#define DC_TELEMETRY_MAGIC 0x3243444au
enum { DC_BOOT = 1, DC_READY = 2, DC_EXIT = 3, DC_ERROR = 4 };
typedef struct {
  uint32_t magic, version, state, source;
  uint32_t triangles, geometry_bytes, system_ram, allocator_live;
  uint32_t allocator_arena, vram_free, frames;
  float yaw, pitch, distance, pvr_fps;
  uint32_t frame_us;
  char error[64];
} dc_scene_telemetry;
typedef char dc_telemetry_layout_check[(sizeof(dc_scene_telemetry) == 128 &&
  offsetof(dc_scene_telemetry, error) == 64 && sizeof(float) == 4) ? 1 : -1];
extern volatile dc_scene_telemetry dc_telemetry;
#endif
