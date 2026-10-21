#include "dgo_stream.h"

#include <inttypes.h>

static void json_string(const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  putchar('"');
  for (; *p; ++p) {
    if (*p == '"' || *p == '\\') printf("\\%c", *p);
    else if (*p < 32 || *p > 126) printf("\\u%04x", (unsigned)*p);
    else putchar(*p);
  }
  putchar('"');
}

static void emit_object(const dc_dgo_object *object, void *context) {
  (void)context;
  printf("{\"kind\":\"object\",\"index\":%" PRIu32 ",\"name\":", object->index);
  json_string(object->name);
  printf(",\"size\":%" PRIu32 ",\"data_offset\":%" PRIu64
         ",\"crc32\":\"%08" PRIx32 "\"}\n",
         object->size, object->data_offset, object->crc32);
}

int main(int argc, char **argv) {
  FILE *file;
  dc_dgo_stats stats;
  const char *error;
  if (argc != 2) {
    fprintf(stderr, "usage: %s ARCHIVE.DGO\n", argv[0]);
    return 2;
  }
  file = fopen(argv[1], "rb");
  if (!file) { perror(argv[1]); return 2; }
  error = dc_dgo_scan(file, &stats, emit_object, NULL);
  fclose(file);
  if (error) {
    fprintf(stderr, "%s: %s at byte %" PRIu64 " after %" PRIu32 " objects\n",
            argv[1], error, stats.bytes_read, stats.completed_objects);
    return 1;
  }
  printf("{\"kind\":\"archive\",\"name\":");
  json_string(stats.name);
  printf(",\"object_count\":%" PRIu32 ",\"bytes\":%" PRIu64
         ",\"payload_bytes\":%" PRIu64 ",\"largest_object\":%" PRIu32 "}\n",
         stats.completed_objects, stats.bytes_read, stats.payload_bytes, stats.largest_object);
  return ferror(stdout) ? 2 : 0;
}
