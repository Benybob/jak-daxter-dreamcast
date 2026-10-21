#include "dgo_stream.h"

#include <string.h>

static uint32_t little_u32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_bytes(FILE *file, void *data, size_t size, dc_dgo_stats *stats) {
  size_t got = fread(data, 1, size, file);
  stats->bytes_read += got;
  return got == size;
}

static int valid_name(const unsigned char *name) {
  size_t i = 0;
  while (i < 60 && name[i]) ++i;
  if (i == 60) return 0;
  while (i < 60) {
    if (name[i++]) return 0;
  }
  return 1;
}

static uint32_t crc_update(uint32_t crc, const unsigned char *p, size_t size) {
  size_t i;
  for (i = 0; i < size; ++i) {
    unsigned bit;
    crc ^= p[i];
    for (bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc;
}

const char *dc_dgo_scan(FILE *file, dc_dgo_stats *stats,
                        dc_dgo_visitor visitor, void *context) {
  unsigned char header[64], buffer[DC_DGO_BUFFER_BYTES];
  uint32_t index;
  int tail;
  memset(stats, 0, sizeof(*stats));
  if (!read_bytes(file, header, sizeof(header), stats)) return "short DGO header";
  if (!memcmp(header, "oZlB", 4)) return "compressed DGO unsupported";
  if (!valid_name(header + 4)) return "invalid DGO name";
  memcpy(stats->name, header + 4, 60);
  stats->declared_objects = little_u32(header);
  if (stats->declared_objects > DC_DGO_MAX_OBJECTS) return "object count exceeds probe limit";

  for (index = 0; index < stats->declared_objects; ++index) {
    dc_dgo_object object;
    uint32_t remaining, crc = 0xffffffffu;
    size_t padding;
    if (!read_bytes(file, header, sizeof(header), stats)) return "short object header";
    if (!valid_name(header + 4)) return "invalid object name";
    memcpy(object.name, header + 4, 60);
    object.index = index;
    object.size = little_u32(header);
    object.data_offset = stats->bytes_read;
    remaining = object.size;
    while (remaining) {
      size_t amount = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
      if (!read_bytes(file, buffer, amount, stats)) return "short object payload";
      crc = crc_update(crc, buffer, amount);
      remaining -= (uint32_t)amount;
    }
    /* Avoid overflowing size + 15 for malformed near-UINT32_MAX sizes. */
    padding = (16u - (object.size & 15u)) & 15u;
    if (!read_bytes(file, buffer, padding, stats)) return "short object padding";
    object.crc32 = crc ^ 0xffffffffu;
    stats->payload_bytes += object.size;
    if (object.size > stats->largest_object) stats->largest_object = object.size;
    ++stats->completed_objects;
    if (visitor) visitor(&object, context);
  }
  tail = fgetc(file);
  if (tail != EOF) {
    ++stats->bytes_read;
    return "unexpected trailing data";
  }
  if (ferror(file)) return "file read error";
  return NULL;
}
