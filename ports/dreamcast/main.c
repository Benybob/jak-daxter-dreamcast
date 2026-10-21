#include <kos.h>
#include <dc/biosfont.h>
#include <dc/maple/controller.h>
#include "dgo_stream.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

static void log_object(const dc_dgo_object *object, void *context) {
  (void)context;
  printf("object %lu: %s, %lu bytes, CRC32 %08lx\n",
         (unsigned long)object->index, object->name,
         (unsigned long)object->size, (unsigned long)object->crc32);
}

int main(void) {
  dc_dgo_stats stats;
  const char *error;
  const char *path = "/cd/JAKPROBE.DGO";
  FILE *file;
  int fixture = 0;
  vid_set_mode(DM_640x480, PM_RGB565);
  vid_clear(8, 16, 24);
  bfont_draw_str_vram_fmt(24, 24, true, "Jak 1 / Dreamcast asset probe");
  bfont_draw_str_vram_fmt(24, 56, true, "Container diagnostic - no gameplay");
  file = fopen(path, "rb");
  if (!file) {
    fixture = 1;
    path = "/rd/probe.dgo";
    file = fopen(path, "rb");
  }
  bfont_draw_str_vram_fmt(24, 104, true, "Source: %s", path);
  if (!file) {
    bfont_draw_str_vram_fmt(24, 144, true, "FAIL: cannot open archive");
  } else {
    error = dc_dgo_scan(file, &stats, log_object, NULL);
    fclose(file);
    printf("%s: %s; completed=%lu; bytes=%llu\n", path,
           error ? error : "OK", (unsigned long)stats.completed_objects,
           (unsigned long long)stats.bytes_read);
    if (error) {
      bfont_draw_str_vram_fmt(24, 144, true, "FAIL: %s", error);
    } else {
      bfont_draw_str_vram_fmt(24, 144, true, "PASS: %lu objects",
                            (unsigned long)stats.completed_objects);
      bfont_draw_str_vram_fmt(24, 176, true, "Largest: %lu bytes",
                            (unsigned long)stats.largest_object);
      bfont_draw_str_vram_fmt(24, 208, true, "Payload buffer: %u bytes", DC_DGO_BUFFER_BYTES);
    }
  }
  bfont_draw_str_vram_fmt(24, 288, true, fixture ?
                        "Synthetic fixture; no game assets" : "External DGO; nothing executed");
  bfont_draw_str_vram_fmt(24, 352, true, "START: exit");
  while (1) {
    maple_device_t *device = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = device ? maple_dev_status(device) : NULL;
    if (state && (state->buttons & CONT_START)) break;
    thd_sleep(20);
  }
  return 0;
}
