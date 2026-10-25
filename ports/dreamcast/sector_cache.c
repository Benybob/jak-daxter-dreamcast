/* DCS1 sector streaming into the native collision world (AI-assisted), ISC.
 * Caps below are compiled in. Header fields that name those caps must match;
 * a larger header value is not honored. */
#include "sector_cache.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
  DC_MANIFEST_BYTES = 108,
  DC_ENTRY_BYTES = 20,
  DC_SECTOR_HEADER_BYTES = 24,
  DC_FACE_BYTES = 44,
  DC_SOURCE_MAX = 65536,
  DC_CELL_MAX = 4096,
  DC_REPLICA_MAX = 262144,
  DC_PER_SECTOR_CAP = 2048,
  DC_UNIQUE_CAP = 8192,
  DC_HASH_SLOTS = 16384,
  DC_WINDOW_CAP = 121,
  DC_PATH_BYTES = 512,
  DC_PATH_SUFFIX = 36,
  DC_CELLS_PER_TRI = 36,
  DC_CANDIDATE_CAP = 1024,
  DC_CACHE_RECORD = 76,
  DC_INPUT_RECORD = 40
};

struct dc_sector_stage {
  dc_world_triangle *rows;
  uint32_t *ids;
  uint16_t *slots;
  uint32_t count;
  uint32_t source_count;
  dc_sector_bounds coverage;
};

struct dc_manifest_info {
  uint32_t source_count, cell_count, replica_count, observed, halo, source_crc;
  int32_t ix_min, ix_max, iz_min, iz_max;
  float cell_size;
};

static uint32_t get_u32(const unsigned char *p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

static int32_t get_i32(const unsigned char *p) {
  uint32_t bits = get_u32(p);
  int32_t value;
  memcpy(&value, &bits, 4);
  return value;
}

static float get_f32(const unsigned char *p) {
  uint32_t bits = get_u32(p);
  float value;
  memcpy(&value, &bits, 4);
  return value;
}

static int f32_is(float value, float expect) {
  uint32_t left, right;
  memcpy(&left, &value, 4);
  memcpy(&right, &expect, 4);
  return left == right;
}

/* Reflected CRC-32, polynomial 0xEDB88320, init and final xor 0xFFFFFFFF.
 * "123456789" finishes as 0xCBF43926. */
static uint32_t crc_feed(uint32_t crc, const unsigned char *bytes, size_t count) {
  size_t i;
  int bit;
  for (i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (bit = 0; bit < 8; ++bit) {
      if (crc & 1u) crc = (crc >> 1) ^ 0xEDB88320u;
      else crc >>= 1;
    }
  }
  return crc;
}

static uint32_t crc_finish(uint32_t crc) {
  return crc ^ 0xFFFFFFFFu;
}

static int crc_self_check(void) {
  static const unsigned char text[9] = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
  };
  uint32_t crc = crc_feed(0xFFFFFFFFu, text, 9);
  return crc_finish(crc) == 0xCBF43926u;
}

static int finite_f(float value) {
  return isfinite(value);
}

static int bounded_finite(float value, float limit) {
  return finite_f(value) && fabsf(value) <= limit;
}

static int box_finite_ordered(dc_sector_bounds box) {
  return finite_f(box.min_x) && finite_f(box.max_x)
      && finite_f(box.min_z) && finite_f(box.max_z)
      && box.min_x <= box.max_x && box.min_z <= box.max_z;
}

/* Exact binary64 difference of the float32 endpoints, plus a few ulps of
 * the larger endpoint. limit is 32.8 for a query and 36.8 for coverage. */
static int width_within(float lo, float hi, double limit) {
  double width = (double)hi - (double)lo;
  double mag = fabs((double)lo);
  double other = fabs((double)hi);
  double ulp;
  if (other > mag) mag = other;
  if (mag < 1.0) mag = 1.0;
  ulp = mag / 8388608.0;
  return width <= limit + ulp * 4.0 + 1e-6;
}

static int box_width_ok(dc_sector_bounds box, double limit) {
  return width_within(box.min_x, box.max_x, limit)
      && width_within(box.min_z, box.max_z, limit);
}

static int world_ready(const dc_collision_world *world) {
  if (!world || world->ready != 1u) return 0;
  if (world->triangles) return world->count > 0u && world->count <= (uint32_t)DC_UNIQUE_CAP;
  return world->count == 0u;
}

static int halo_of(float cell, uint32_t *halo) {
  double value;
  if (!finite_f(cell) || cell < 4.f || cell > 64.f) return 0;
  value = floor(22.4 / (double)cell) + 1.0;
  if (value < 1.0 || value > 4294967295.0) return 0;
  *halo = (uint32_t)value;
  return 1;
}

static int record_layout_ok(void) {
  unsigned char bytes[4] = {1, 2, 3, 4};
  uint32_t value;
  memcpy(&value, bytes, 4);
  if (value != 0x04030201u) return 0;
  if (sizeof(dc_world_triangle) != 40u) return 0;
  if (offsetof(dc_world_triangle, a) != 0u) return 0;
  if (offsetof(dc_world_triangle, b) != 12u) return 0;
  if (offsetof(dc_world_triangle, c) != 24u) return 0;
  if (offsetof(dc_world_triangle, pat) != 36u) return 0;
  return 1;
}

static int index_usable(const dc_sector_index *index) {
  uint32_t halo = 0;
  if (!index || !index->entries) return 0;
  if (index->count < 1u || index->count > (uint32_t)DC_CELL_MAX) return 0;
  if (index->source_count < 1u || index->source_count > (uint32_t)DC_SOURCE_MAX) return 0;
  if (!halo_of(index->cell_size, &halo) || halo != index->halo) return 0;
  if ((int64_t)index->ix_max < (int64_t)index->ix_min) return 0;
  if ((int64_t)index->iz_max < (int64_t)index->iz_min) return 0;
  return 1;
}

static const char *check_manifest_header(const unsigned char *header, struct dc_manifest_info *info) {
  uint32_t version, fr3, flags, per_sector, cells_per_tri, total_cells, replica_cap;
  uint32_t cache_cap, candidate_cap, cache_record, input_record, expect_halo = 0;
  float max_span, camera, displacement, radius;
  if (memcmp(header, "DCS1", 4) != 0) return "invalid sector manifest header";
  version = get_u32(header + 4);
  fr3 = get_u32(header + 8);
  flags = get_u32(header + 12);
  info->source_count = get_u32(header + 16);
  info->cell_size = get_f32(header + 20);
  info->cell_count = get_u32(header + 24);
  info->replica_count = get_u32(header + 28);
  info->observed = get_u32(header + 32);
  per_sector = get_u32(header + 36);
  cells_per_tri = get_u32(header + 40);
  total_cells = get_u32(header + 44);
  replica_cap = get_u32(header + 48);
  info->ix_min = get_i32(header + 52);
  info->ix_max = get_i32(header + 56);
  info->iz_min = get_i32(header + 60);
  info->iz_max = get_i32(header + 64);
  info->halo = get_u32(header + 68);
  info->source_crc = get_u32(header + 72);
  max_span = get_f32(header + 76);
  camera = get_f32(header + 80);
  displacement = get_f32(header + 84);
  radius = get_f32(header + 88);
  cache_cap = get_u32(header + 92);
  candidate_cap = get_u32(header + 96);
  cache_record = get_u32(header + 100);
  input_record = get_u32(header + 104);
  if (version != 1u || fr3 != 43u || flags != 1u) return "invalid sector manifest header";
  if (info->source_count < 1u || info->source_count > (uint32_t)DC_SOURCE_MAX)
    return "invalid sector manifest header";
  if (info->cell_count < 1u || info->cell_count > (uint32_t)DC_CELL_MAX)
    return "invalid sector manifest header";
  if (info->replica_count < 1u || info->replica_count > (uint32_t)DC_REPLICA_MAX)
    return "invalid sector manifest header";
  if (info->observed < 1u || info->observed > (uint32_t)DC_PER_SECTOR_CAP)
    return "invalid sector manifest header";
  if (per_sector != (uint32_t)DC_PER_SECTOR_CAP || cells_per_tri != (uint32_t)DC_CELLS_PER_TRI)
    return "invalid sector manifest header";
  if (total_cells != (uint32_t)DC_CELL_MAX || replica_cap != (uint32_t)DC_REPLICA_MAX)
    return "invalid sector manifest header";
  if ((int64_t)info->ix_max < (int64_t)info->ix_min) return "invalid sector manifest header";
  if ((int64_t)info->iz_max < (int64_t)info->iz_min) return "invalid sector manifest header";
  if (!halo_of(info->cell_size, &expect_halo) || info->halo != expect_halo)
    return "invalid sector manifest header";
  if (!f32_is(max_span, 64.f) || !f32_is(camera, 10.4f)) return "invalid sector manifest header";
  if (!f32_is(displacement, 8.f) || !f32_is(radius, 2.f)) return "invalid sector manifest header";
  if (cache_cap != (uint32_t)DC_UNIQUE_CAP || candidate_cap != (uint32_t)DC_CANDIDATE_CAP)
    return "invalid sector manifest header";
  if (cache_record != (uint32_t)DC_CACHE_RECORD || input_record != (uint32_t)DC_INPUT_RECORD)
    return "invalid sector manifest header";
  return NULL;
}

const char *dc_sector_index_load(FILE *file, dc_sector_index *out) {
  unsigned char header[DC_MANIFEST_BYTES], raw[DC_ENTRY_BYTES], foot[4];
  struct dc_manifest_info info;
  dc_sector_entry *entries = NULL;
  const char *error;
  uint32_t crc, i, sum, max_count, final_crc, file_crc;
  int32_t prev_ix = 0, prev_iz = 0, got_min_x = 0, got_max_x = 0, got_min_z = 0, got_max_z = 0;
  int ch, have_prev = 0;

  if (!out) return "invalid sector index arguments";
  memset(out, 0, sizeof *out);
  memset(&info, 0, sizeof info);
  if (!file) return "invalid sector index arguments";
  if (!crc_self_check()) return "sector crc self-check failed";
  if (fread(header, 1, sizeof header, file) != sizeof header) return "truncated sector manifest";
  crc = crc_feed(0xFFFFFFFFu, header, sizeof header);
  error = check_manifest_header(header, &info);
  if (error) return error;

  entries = (dc_sector_entry *)malloc((size_t)info.cell_count * sizeof *entries);
  if (!entries) return "sector manifest allocation failed";
  sum = 0;
  max_count = 0;
  for (i = 0; i < info.cell_count; ++i) {
    int32_t ix, iz;
    uint32_t count, entry_crc, bytes, expect;
    if (fread(raw, 1, sizeof raw, file) != sizeof raw) {
      error = "truncated sector manifest";
      goto fail;
    }
    crc = crc_feed(crc, raw, sizeof raw);
    ix = get_i32(raw);
    iz = get_i32(raw + 4);
    count = get_u32(raw + 8);
    entry_crc = get_u32(raw + 12);
    bytes = get_u32(raw + 16);
    if (count < 1u || count > (uint32_t)DC_PER_SECTOR_CAP) {
      error = "invalid sector manifest entry";
      goto fail;
    }
    expect = 24u + 44u * count + 4u;
    if (bytes != expect) {
      error = "invalid sector manifest entry";
      goto fail;
    }
    if (have_prev && (ix < prev_ix || (ix == prev_ix && iz <= prev_iz))) {
      error = "invalid sector manifest entry";
      goto fail;
    }
    if (!have_prev) {
      got_min_x = got_max_x = ix;
      got_min_z = got_max_z = iz;
    } else {
      if (ix < got_min_x) got_min_x = ix;
      if (ix > got_max_x) got_max_x = ix;
      if (iz < got_min_z) got_min_z = iz;
      if (iz > got_max_z) got_max_z = iz;
    }
    if (count > max_count) max_count = count;
    if (sum > (uint32_t)DC_REPLICA_MAX - count) {
      error = "invalid sector manifest totals";
      goto fail;
    }
    sum += count;
    entries[i].ix = ix;
    entries[i].iz = iz;
    entries[i].count = count;
    entries[i].crc = entry_crc;
    entries[i].bytes = bytes;
    prev_ix = ix;
    prev_iz = iz;
    have_prev = 1;
  }
  if (sum != info.replica_count || max_count != info.observed
      || got_min_x != info.ix_min || got_max_x != info.ix_max
      || got_min_z != info.iz_min || got_max_z != info.iz_max) {
    error = "invalid sector manifest totals";
    goto fail;
  }
  if (fread(foot, 1, sizeof foot, file) != sizeof foot) {
    error = "truncated sector manifest";
    goto fail;
  }
  file_crc = get_u32(foot);
  final_crc = crc_finish(crc);
  if (file_crc != final_crc) {
    error = "sector manifest checksum mismatch";
    goto fail;
  }
  ch = fgetc(file);
  if (ferror(file)) {
    error = "truncated sector manifest";
    goto fail;
  }
  if (ch != EOF) {
    error = "trailing sector manifest bytes";
    goto fail;
  }

  out->source_count = info.source_count;
  out->source_crc = info.source_crc;
  out->package_crc = file_crc;
  out->count = info.cell_count;
  out->cell_size = info.cell_size;
  out->ix_min = info.ix_min;
  out->ix_max = info.ix_max;
  out->iz_min = info.iz_min;
  out->iz_max = info.iz_max;
  out->halo = info.halo;
  out->entries = entries;
  return NULL;

fail:
  free(entries);
  memset(out, 0, sizeof *out);
  return error;
}

void dc_sector_index_free(dc_sector_index *index) {
  if (!index) return;
  free(index->entries);
  memset(index, 0, sizeof *index);
}

int dc_sector_covers(const dc_sector_cache *cache, dc_sector_bounds query) {
  if (!cache || cache->valid != 1u || !world_ready(&cache->world)) return 0;
  if (!box_finite_ordered(cache->coverage) || !box_width_ok(cache->coverage, 36.8)) return 0;
  if (!box_finite_ordered(query)) return 0;
  if (query.min_x < cache->coverage.min_x || query.max_x > cache->coverage.max_x) return 0;
  if (query.min_z < cache->coverage.min_z || query.max_z > cache->coverage.max_z) return 0;
  return 1;
}

static const char *root_ok(const char *root) {
  size_t length;
  if (!root || root[0] == '\0') return "invalid sector root";
  length = strlen(root);
  if (length > (size_t)(DC_PATH_BYTES - 1 - DC_PATH_SUFFIX)) return "invalid sector root";
  return NULL;
}

static const char *sector_path(char *dst, size_t dst_size, const char *root, int32_t ix, int32_t iz) {
  int wrote = snprintf(dst, dst_size, "%s/sectors/%ld_%ld.dcs", root, (long)ix, (long)iz);
  if (wrote < 0 || (size_t)wrote >= dst_size) return "invalid sector root";
  return NULL;
}

/* Closed coverage neighborhood: lo = ceil(min/cell) - 1, hi = floor(max/cell). */
static int cell_bounds(double min_v, double max_v, double cell, int32_t *lo, int32_t *hi) {
  double q0, q1, low, high;
  if (!(cell > 0.0) || !isfinite(min_v) || !isfinite(max_v)) return 0;
  q0 = min_v / cell;
  q1 = max_v / cell;
  if (!isfinite(q0) || !isfinite(q1)) return 0;
  low = ceil(q0) - 1.0;
  high = floor(q1);
  if (!isfinite(low) || !isfinite(high)) return 0;
  if (low < -2147483648.0 || high > 2147483647.0 || high < low) return 0;
  *lo = (int32_t)low;
  *hi = (int32_t)high;
  return 1;
}

static const dc_sector_entry *find_cell(const dc_sector_index *index, int32_t ix, int32_t iz) {
  uint32_t lo = 0, hi = index->count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2u;
    const dc_sector_entry *entry = &index->entries[mid];
    if (entry->ix < ix || (entry->ix == ix && entry->iz < iz)) lo = mid + 1u;
    else hi = mid;
  }
  if (lo < index->count && index->entries[lo].ix == ix && index->entries[lo].iz == iz)
    return &index->entries[lo];
  return NULL;
}

static int tri_world_ok(const dc_world_triangle *tri) {
  return bounded_finite(tri->a.x, 1000000.f) && bounded_finite(tri->a.y, 1000000.f)
      && bounded_finite(tri->a.z, 1000000.f)
      && bounded_finite(tri->b.x, 1000000.f) && bounded_finite(tri->b.y, 1000000.f)
      && bounded_finite(tri->b.z, 1000000.f)
      && bounded_finite(tri->c.x, 1000000.f) && bounded_finite(tri->c.y, 1000000.f)
      && bounded_finite(tri->c.z, 1000000.f);
}

/* Corrected PAT mode is bits [5:3]. Other bits stay in the stored word. */
static int pat_mode_ok(uint32_t pat) {
  return ((pat >> 3) & 7u) <= 2u;
}

static int xz_hit(const dc_world_triangle *tri, dc_sector_bounds coverage) {
  float min_x = tri->a.x, max_x = tri->a.x, min_z = tri->a.z, max_z = tri->a.z;
  if (tri->b.x < min_x) min_x = tri->b.x;
  if (tri->c.x < min_x) min_x = tri->c.x;
  if (tri->b.x > max_x) max_x = tri->b.x;
  if (tri->c.x > max_x) max_x = tri->c.x;
  if (tri->b.z < min_z) min_z = tri->b.z;
  if (tri->c.z < min_z) min_z = tri->c.z;
  if (tri->b.z > max_z) max_z = tri->b.z;
  if (tri->c.z > max_z) max_z = tri->c.z;
  return min_x <= coverage.max_x && max_x >= coverage.min_x
      && min_z <= coverage.max_z && max_z >= coverage.min_z;
}

static const char *dedup_consider(struct dc_sector_stage *stage, uint32_t id,
                                  const dc_world_triangle *tri, int include) {
  uint32_t slot = (id * 2654435761u) & (uint32_t)(DC_HASH_SLOTS - 1);
  uint32_t probes;
  for (probes = 0; probes < (uint32_t)DC_HASH_SLOTS; ++probes) {
    uint16_t current = stage->slots[slot];
    if (current == 0xFFFFu) {
      if (!include) return NULL;
      if (stage->count >= (uint32_t)DC_UNIQUE_CAP) return "sector unique triangle cap exceeded";
      stage->slots[slot] = (uint16_t)stage->count;
      stage->ids[stage->count] = id;
      stage->rows[stage->count] = *tri;
      stage->count += 1u;
      return NULL;
    }
    if (stage->ids[current] == id) {
      if (memcmp(&stage->rows[current], tri, sizeof *tri) != 0)
        return "sector replica payload mismatch";
      return NULL;
    }
    slot = (slot + 1u) & (uint32_t)(DC_HASH_SLOTS - 1);
  }
  return "sector unique triangle cap exceeded";
}

static const char *read_exact(FILE *fp, unsigned char *dst, size_t count, uint32_t *crc) {
  if (fread(dst, 1, count, fp) != count) return "truncated sector file";
  *crc = crc_feed(*crc, dst, count);
  return NULL;
}

static const char *read_sector(FILE *fp, const dc_sector_entry *entry, struct dc_sector_stage *stage) {
  unsigned char header[DC_SECTOR_HEADER_BYTES], record[DC_FACE_BYTES], foot[4];
  const char *error;
  uint32_t crc = 0xFFFFFFFFu, version, count, payload, i, previous = 0, file_crc, final_crc;
  int32_t ix, iz;
  int ch, seen = 0;
  error = read_exact(fp, header, sizeof header, &crc);
  if (error) return error;
  if (memcmp(header, "DCSS", 4) != 0) return "invalid sector header";
  version = get_u32(header + 4);
  ix = get_i32(header + 8);
  iz = get_i32(header + 12);
  count = get_u32(header + 16);
  payload = get_u32(header + 20);
  if (version != 1u || ix != entry->ix || iz != entry->iz) return "invalid sector header";
  if (count != entry->count || payload != count * 44u) return "invalid sector header";
  if (entry->bytes != 24u + 44u * count + 4u) return "invalid sector header";
  for (i = 0; i < count; ++i) {
    dc_world_triangle tri;
    uint32_t id;
    int include;
    error = read_exact(fp, record, sizeof record, &crc);
    if (error) return error;
    id = get_u32(record);
    memcpy(&tri, record + 4, sizeof tri);
    if ((seen && id <= previous) || id >= stage->source_count) return "invalid sector record";
    if (!tri_world_ok(&tri) || !pat_mode_ok(tri.pat)) return "invalid sector record";
    previous = id;
    seen = 1;
    include = xz_hit(&tri, stage->coverage);
    error = dedup_consider(stage, id, &tri, include);
    if (error) return error;
  }
  if (fread(foot, 1, sizeof foot, fp) != sizeof foot) return "truncated sector file";
  file_crc = get_u32(foot);
  final_crc = crc_finish(crc);
  if (file_crc != final_crc || file_crc != entry->crc) return "sector checksum mismatch";
  ch = fgetc(fp);
  if (ferror(fp)) return "truncated sector file";
  if (ch != EOF) return "trailing sector bytes";
  return NULL;
}

static void swap_row(uint32_t *ids, dc_world_triangle *rows, uint32_t a, uint32_t b) {
  uint32_t id = ids[a];
  dc_world_triangle tri = rows[a];
  ids[a] = ids[b];
  rows[a] = rows[b];
  ids[b] = id;
  rows[b] = tri;
}

static void heap_sift(uint32_t *ids, dc_world_triangle *rows, uint32_t root, uint32_t count) {
  for (;;) {
    uint32_t left = root * 2u + 1u;
    uint32_t best, right;
    if (left >= count) break;
    best = left;
    right = left + 1u;
    if (right < count && ids[right] > ids[left]) best = right;
    if (ids[root] >= ids[best]) break;
    swap_row(ids, rows, root, best);
    root = best;
  }
}

static void heap_sort(uint32_t *ids, dc_world_triangle *rows, uint32_t count) {
  uint32_t start, end;
  if (count < 2u) return;
  start = count / 2u;
  while (start > 0u) {
    --start;
    heap_sift(ids, rows, start, count);
  }
  end = count;
  while (end > 1u) {
    --end;
    swap_row(ids, rows, 0u, end);
    heap_sift(ids, rows, 0u, end);
  }
}

static const char *rebase_rows(dc_world_triangle *rows, uint32_t count, dc_vec3 origin) {
  uint32_t i;
  for (i = 0; i < count; ++i) {
    rows[i].a.x -= origin.x;
    rows[i].a.y -= origin.y;
    rows[i].a.z -= origin.z;
    rows[i].b.x -= origin.x;
    rows[i].b.y -= origin.y;
    rows[i].b.z -= origin.z;
    rows[i].c.x -= origin.x;
    rows[i].c.y -= origin.y;
    rows[i].c.z -= origin.z;
    if (!bounded_finite(rows[i].a.x, 4096.f) || !bounded_finite(rows[i].a.y, 4096.f)
        || !bounded_finite(rows[i].a.z, 4096.f)
        || !bounded_finite(rows[i].b.x, 4096.f) || !bounded_finite(rows[i].b.y, 4096.f)
        || !bounded_finite(rows[i].b.z, 4096.f)
        || !bounded_finite(rows[i].c.x, 4096.f) || !bounded_finite(rows[i].c.y, 4096.f)
        || !bounded_finite(rows[i].c.z, 4096.f))
      return "rebased triangle exceeds local limit";
  }
  return NULL;
}

static void install_world(dc_sector_cache *cache, dc_collision_world *built,
                          dc_vec3 origin, dc_sector_bounds coverage, uint32_t package_crc) {
  dc_world_cached_triangle *previous = cache->world.triangles;
  cache->world = *built;
  cache->origin = origin;
  cache->coverage = coverage;
  cache->valid = 1u;
  cache->package_crc = package_crc;
  built->triangles = NULL;
  built->count = 0u;
  built->ready = 0u;
  free(previous);
}

const char *dc_sector_retarget(const dc_sector_index *index, const char *root,
                               dc_sector_bounds query, dc_vec3 origin,
                               dc_sector_cache *cache) {
  const dc_sector_entry *chosen[DC_WINDOW_CAP];
  dc_world_triangle *rows = NULL;
  uint32_t *ids = NULL;
  uint16_t *slots = NULL;
  dc_collision_world built;
  struct dc_sector_stage stage;
  dc_sector_bounds coverage;
  const char *error = NULL;
  char path[DC_PATH_BYTES];
  FILE *fp = NULL;
  int32_t x_lo = 0, x_hi = 0, z_lo = 0, z_hi = 0, ix;
  int64_t axis, nx, nz, cells, region_x0, region_x1, region_z0, region_z1;
  uint32_t chosen_count = 0, s;
  double axis_value, cell;
  if (!cache || !index_usable(index)) return "invalid sector retarget arguments";
  error = root_ok(root);
  if (error) return error;
  if (!box_finite_ordered(query) || !box_width_ok(query, 32.8)) return "invalid sector query";
  if (!bounded_finite(origin.x, 1000000.f) || !bounded_finite(origin.y, 1000000.f)
      || !bounded_finite(origin.z, 1000000.f))
    return "invalid sector origin";
  if (cache->package_crc == index->package_crc && dc_sector_covers(cache, query)) return NULL;

  if (!record_layout_ok()) return "unsupported sector record layout";
  coverage.min_x = query.min_x - 2.f;
  coverage.max_x = query.max_x + 2.f;
  coverage.min_z = query.min_z - 2.f;
  coverage.max_z = query.max_z + 2.f;
  if (!box_finite_ordered(coverage) || !box_width_ok(coverage, 36.8)) return "invalid sector query";

  cell = (double)index->cell_size;
  if (!cell_bounds((double)coverage.min_x, (double)coverage.max_x, cell, &x_lo, &x_hi)
      || !cell_bounds((double)coverage.min_z, (double)coverage.max_z, cell, &z_lo, &z_hi))
    return "missing sector region";
  axis_value = floor(36.8 / cell) + 2.0;
  if (!isfinite(axis_value) || axis_value < 1.0 || axis_value > 11.0)
    return "sector cell span exceeds limit";
  axis = (int64_t)axis_value;
  nx = (int64_t)x_hi - (int64_t)x_lo + 1;
  nz = (int64_t)z_hi - (int64_t)z_lo + 1;
  if (nx < 1 || nz < 1 || nx > axis || nz > axis) return "sector cell span exceeds limit";
  cells = nx * nz;
  if (cells > axis * axis || cells > (int64_t)DC_WINDOW_CAP) return "sector cell span exceeds limit";
  region_x0 = (int64_t)index->ix_min - (int64_t)index->halo;
  region_x1 = (int64_t)index->ix_max + (int64_t)index->halo;
  region_z0 = (int64_t)index->iz_min - (int64_t)index->halo;
  region_z1 = (int64_t)index->iz_max + (int64_t)index->halo;
  if ((int64_t)x_lo < region_x0 || (int64_t)x_hi > region_x1
      || (int64_t)z_lo < region_z0 || (int64_t)z_hi > region_z1)
    return "missing sector region";

  ix = x_lo;
  for (;;) {
    int32_t iz = z_lo;
    for (;;) {
      const dc_sector_entry *entry = find_cell(index, ix, iz);
      if (entry) {
        if (entry->count < 1u || entry->count > (uint32_t)DC_PER_SECTOR_CAP
            || entry->bytes != 24u + 44u * entry->count + 4u)
          return "invalid sector retarget arguments";
        if (chosen_count >= (uint32_t)DC_WINDOW_CAP) return "sector cell span exceeds limit";
        chosen[chosen_count++] = entry;
      }
      if (iz == z_hi) break;
      ++iz;
    }
    if (ix == x_hi) break;
    ++ix;
  }

  memset(&built, 0, sizeof built);
  memset(&stage, 0, sizeof stage);
  if (chosen_count > 0u) {
    uint32_t unique;
    if (!crc_self_check()) return "sector crc self-check failed";
    rows = (dc_world_triangle *)malloc(sizeof *rows * (size_t)DC_UNIQUE_CAP);
    ids = (uint32_t *)malloc(sizeof *ids * (size_t)DC_UNIQUE_CAP);
    slots = (uint16_t *)malloc(sizeof *slots * (size_t)DC_HASH_SLOTS);
    if (!rows || !ids || !slots) {
      error = "sector workspace allocation failed";
      goto fail;
    }
    memset(slots, 0xFF, sizeof *slots * (size_t)DC_HASH_SLOTS);
    stage.rows = rows;
    stage.ids = ids;
    stage.slots = slots;
    stage.count = 0;
    stage.source_count = index->source_count;
    stage.coverage = coverage;
    for (s = 0; s < chosen_count; ++s) {
      error = sector_path(path, sizeof path, root, chosen[s]->ix, chosen[s]->iz);
      if (error) goto fail;
      fp = fopen(path, "rb");
      if (!fp) {
        error = "missing sector file";
        goto fail;
      }
      error = read_sector(fp, chosen[s], &stage);
      if (fclose(fp) != 0 && !error) error = "sector close failed";
      fp = NULL;
      if (error) goto fail;
    }
    unique = stage.count;
    if (unique > 1u) heap_sort(ids, rows, unique);
    if (unique > 0u) {
      error = rebase_rows(rows, unique, origin);
      if (error) goto fail;
      error = dc_collision_build(&built, rows, unique);
    } else {
      error = dc_collision_empty(&built);
    }
    if (error) goto fail;
  } else {
    error = dc_collision_empty(&built);
    if (error) goto fail;
  }

  install_world(cache, &built, origin, coverage, index->package_crc);
  free(rows);
  free(ids);
  free(slots);
  return NULL;

fail:
  if (fp) fclose(fp);
  free(rows);
  free(ids);
  free(slots);
  dc_collision_free(&built);
  return error ? error : "invalid sector retarget arguments";
}

void dc_sector_cache_free(dc_sector_cache *cache) {
  if (!cache) return;
  dc_collision_free(&cache->world);
  memset(cache, 0, sizeof *cache);
}
