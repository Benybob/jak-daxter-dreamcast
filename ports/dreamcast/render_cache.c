/* Transactional CPU geometry-window cache (AI-assisted), ISC.
 * One block is resident at a time. Batch payloads, the staged result, and the
 * previous window can overlap that block. Nothing here is GPU memory.
 *
 * Worst case inside the verified reader caps (cell >= 4, query extent <= 36.8):
 *   selected files S <= 1+121 = 122
 *   distinct source ids U <= S*4096 = 499712 = 61*8192
 *   batches B = ceil(U/8192) <= 61
 *   opens <= S*(2*B+1) (select + validate, possible empty final select)
 *   max opens = 122*123 = 15006; no source-count-sized allocation
 * Peak CPU heap while a sector is inside dc_render_block_load:
 *   old faces <= 8192*116
 *   staged faces <= 8192*116
 *   batch payloads <= 8192*116
 *   workspace == sizeof(dc_render_workspace) (109920)
 *   reader raw <= 475136 and compressed <= 476160 (both only during the call)
 *   catalog storage stays with its owner (manifest cap about 2.4 MiB)
 * After return the cache retains only the packed face buffer (or none), the
 * root copy, and the source_sha copy.
 */
#include "render_cache.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef DC_RENDER_CACHE_TEST_IO
extern FILE *dc_render_cache_test_fopen(const char *path, const char *mode);
extern int dc_render_cache_test_fclose(FILE *file);
extern void *dc_render_cache_test_malloc(size_t size);
extern void dc_render_cache_test_free(void *ptr);
#define CACHE_FOPEN dc_render_cache_test_fopen
#define CACHE_FCLOSE dc_render_cache_test_fclose
#define CACHE_MALLOC dc_render_cache_test_malloc
#define CACHE_FREE dc_render_cache_test_free
#else
#define CACHE_FOPEN fopen
#define CACHE_FCLOSE fclose
#define CACHE_MALLOC malloc
#define CACHE_FREE free
#endif

typedef char dc_render_cache_abi[(sizeof(double) == 8 && DBL_MANT_DIG == 53 &&
    sizeof(float) == 4 && FLT_MANT_DIG == 24 &&
    sizeof(uint32_t) == 4 && sizeof(int32_t) == 4 &&
    DC_RENDER_FACE_BYTES == 116 && DC_RENDER_WINDOW_CAP == 8192 &&
    DC_RENDER_FILE_CAP == 4096 &&
    DC_RENDER_CACHE_AXIS_CAP * DC_RENDER_CACHE_AXIS_CAP == DC_RENDER_CACHE_LOCAL_CAP &&
    DC_RENDER_CACHE_PATH_MAX >= DC_RENDER_CACHE_ROOT_MAX + 40) ? 1 : -1];

enum { ROLE_GLOBAL = 1, ROLE_LOCAL = 2 };

typedef struct dc_render_workspace {
    uint32_t batch[DC_RENDER_WINDOW_CAP];
    uint32_t merged[DC_RENDER_WINDOW_CAP];
    uint32_t file_ids[DC_RENDER_FILE_CAP];
    uint32_t local_count;
    uint32_t batch_count;
    dc_render_entry global_entry;
    dc_render_entry locals[DC_RENDER_CACHE_LOCAL_CAP];
    uint8_t seen[DC_RENDER_WINDOW_CAP];
    uint8_t role[DC_RENDER_WINDOW_CAP];
    uint8_t included[DC_RENDER_WINDOW_CAP];
} dc_render_workspace;

typedef char dc_render_cache_workspace_size[(sizeof(dc_render_entry) == 28 &&
    sizeof(dc_render_workspace) ==
        sizeof(uint32_t) * (DC_RENDER_WINDOW_CAP * 2u + DC_RENDER_FILE_CAP + 2u) +
        sizeof(dc_render_entry) * (DC_RENDER_CACHE_LOCAL_CAP + 1u) +
        DC_RENDER_WINDOW_CAP * 3u &&
    sizeof(dc_render_workspace) == 109920u) ? 1 : -1];

typedef struct dc_render_identity {
    uint32_t catalog_crc, catalog_bytes, package_bytes;
    uint32_t source_count, sector_count, global_count, replica_count;
    uint32_t material_count, texture_count, texture_bytes, mix;
    float cell_size;
} dc_render_identity;

typedef struct dc_render_job {
    const dc_render_catalog *catalog;
    const dc_render_block_source *source;
    const char *root;
    const dc_render_query *query;
    dc_render_workspace *ws;
    unsigned char *payloads;
    unsigned char *stage;
    uint32_t stage_count;
    uint32_t stage_cap;
    uint32_t cursor;
    uint32_t source_count;
    double cell;
} dc_render_job;

static uint32_t cache_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float cache_f32(const unsigned char *p) {
    uint32_t bits = cache_u32(p);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

static uint32_t cache_mix(uint32_t hash, uint32_t value) {
    hash ^= value;
    return hash * 16777619u;
}

static uint32_t cache_mix_i32(uint32_t hash, int32_t value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    return cache_mix(hash, bits);
}

static int cache_finite_cell(float cell) {
    return isfinite(cell) && cell >= 4.f && cell <= 64.f;
}

static int cache_query_ok(const dc_render_query *query) {
    if (!query) return 0;
    if (!isfinite(query->x0) || !isfinite(query->z0) || !isfinite(query->x1) || !isfinite(query->z1))
        return 0;
    if (query->x0 > query->x1 || query->z0 > query->z1) return 0;
    if (query->x0 < -DC_RENDER_CACHE_COORD_MAX || query->x1 > DC_RENDER_CACHE_COORD_MAX ||
        query->z0 < -DC_RENDER_CACHE_COORD_MAX || query->z1 > DC_RENDER_CACHE_COORD_MAX)
        return 0;
    if ((query->x1 - query->x0) > DC_RENDER_CACHE_QUERY_MAX + 0.001 ||
        (query->z1 - query->z0) > DC_RENDER_CACHE_QUERY_MAX + 0.001)
        return 0;
    return 1;
}

static int cache_root_ok(const char *root) {
    size_t length;
    if (!root) return 0;
    for(length=0;length<=DC_RENDER_CACHE_ROOT_MAX&&root[length];++length){}
    if (length == 0 || length > DC_RENDER_CACHE_ROOT_MAX) return 0;
    return 1;
}

static int cache_hex(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static int cache_sha_ok(const char *sha,const dc_render_catalog *catalog) {
    size_t i, length;
    if (!sha) return 0;
    for(length=0;length<=DC_RENDER_CACHE_SHA_MAX&&sha[length];++length){}
    if(length!=64u)return 0;
    for (i = 0; i < 32u; ++i) {
        int high=cache_hex(sha[2u*i]),low=cache_hex(sha[2u*i+1u]);
        if(high<0||low<0||(unsigned)(high*16+low)!=catalog->storage[164u+i])return 0;
    }
    return 1;
}

static int cache_bounded_text(const char *text, size_t max) {
    size_t i;
    if (!text) return 0;
    for (i = 0; i < max; ++i) {
        if (text[i] == '\0') return i > 0;
    }
    return 0;
}

static int cache_append_text(char *out, int cap, int pos, const char *text) {
    if (pos < 0) return -1;
    while (*text) {
        if (pos + 1 >= cap) return -1;
        out[pos++] = *text++;
    }
    if (pos >= cap) return -1;
    out[pos] = '\0';
    return pos;
}

static int cache_append_i32(char *out, int cap, int pos, int32_t value) {
    char digits[16];
    uint32_t magnitude;
    int count = 0;
    if (pos < 0) return -1;
    if (value < 0) {
        if (pos + 1 >= cap) return -1;
        out[pos++] = '-';
        magnitude = (value == INT32_MIN) ? 2147483648u : (uint32_t)(-value);
    } else magnitude = (uint32_t)value;
    do {
        digits[count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude);
    while (count) {
        if (pos + 1 >= cap) return -1;
        out[pos++] = digits[--count];
    }
    if (pos >= cap) return -1;
    out[pos] = '\0';
    return pos;
}

static const char *cache_path_global(char *out, const char *root) {
    if (cache_append_text(out, DC_RENDER_CACHE_PATH_MAX, 0, root) < 0) return DC_RENDER_CACHE_ERR_PATH;
    if (cache_append_text(out, DC_RENDER_CACHE_PATH_MAX, (int)strlen(out),
        root[strlen(root)-1u]=='/'||root[strlen(root)-1u]=='\\'?"global.drs":"/global.drs") < 0)
        return DC_RENDER_CACHE_ERR_PATH;
    return NULL;
}

static const char *cache_path_local(char *out, const char *root, int32_t ix, int32_t iz) {
    int pos = cache_append_text(out, DC_RENDER_CACHE_PATH_MAX, 0, root);
    pos = cache_append_text(out, DC_RENDER_CACHE_PATH_MAX, pos,
        root[strlen(root)-1u]=='/'||root[strlen(root)-1u]=='\\'?"sectors/":"/sectors/");
    pos = cache_append_i32(out, DC_RENDER_CACHE_PATH_MAX, pos, ix);
    pos = cache_append_text(out, DC_RENDER_CACHE_PATH_MAX, pos, "_");
    pos = cache_append_i32(out, DC_RENDER_CACHE_PATH_MAX, pos, iz);
    pos = cache_append_text(out, DC_RENDER_CACHE_PATH_MAX, pos, ".drs");
    if (pos < 0) return DC_RENDER_CACHE_ERR_PATH;
    return NULL;
}

static int cache_rect_touches(double x0, double x1, double z0, double z1, const dc_render_query *query) {
    return x0 <= query->x1 && x1 >= query->x0 && z0 <= query->z1 && z1 >= query->z0;
}

static const char *cache_cell_bounds(int32_t ix, int32_t iz, double cell,
                                    double *x0, double *x1, double *z0, double *z1) {
    *x0 = (double)ix * cell;
    *z0 = (double)iz * cell;
    *x1 = *x0 + cell;
    *z1 = *z0 + cell;
    if (!isfinite(*x0) || !isfinite(*x1) || !isfinite(*z0) || !isfinite(*z1))
        return DC_RENDER_CACHE_ERR_COVERAGE;
    return NULL;
}

static int cache_face_touches(const unsigned char *record, const dc_render_query *query) {
    double min_x = 0.0, max_x = 0.0, min_z = 0.0, max_z = 0.0;
    uint32_t vertex;
    for (vertex = 0; vertex < 3u; ++vertex) {
        const unsigned char *point = record + 8u + vertex * 36u;
        double x = (double)cache_f32(point);
        double z = (double)cache_f32(point + 8u);
        if (!isfinite(x) || !isfinite(z)) return -1;
        if (vertex == 0u) {
            min_x = max_x = x;
            min_z = max_z = z;
        } else {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (z < min_z) min_z = z;
            if (z > max_z) max_z = z;
        }
    }
    return cache_rect_touches(min_x, max_x, min_z, max_z, query);
}

static uint32_t cache_mix_entry(uint32_t hash, const dc_render_entry *entry) {
    hash = cache_mix_i32(hash, entry->ix);
    hash = cache_mix_i32(hash, entry->iz);
    hash = cache_mix(hash, entry->count);
    hash = cache_mix(hash, entry->raw_bytes);
    hash = cache_mix(hash, entry->file_bytes);
    hash = cache_mix(hash, entry->payload_crc);
    hash = cache_mix(hash, entry->compressed_crc);
    return hash;
}

static const char *cache_capture(const dc_render_catalog *catalog, dc_render_identity *out) {
    uint32_t hash = 2166136261u, index, cell_bits;
    dc_render_entry entry;
    memset(out, 0, sizeof(*out));
    if (!dc_render_global_get(catalog, &entry)) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    out->catalog_crc = catalog->crc;
    out->catalog_bytes = catalog->bytes;
    out->package_bytes = catalog->package_bytes;
    out->source_count = catalog->source_count;
    out->sector_count = catalog->sector_count;
    out->global_count = catalog->global_count;
    out->replica_count = catalog->replica_count;
    out->material_count = catalog->material_count;
    out->texture_count = catalog->texture_count;
    out->texture_bytes = catalog->texture_bytes;
    out->cell_size = catalog->cell_size;
    memcpy(&cell_bits, &catalog->cell_size, 4);
    hash = cache_mix(hash, out->catalog_crc);
    hash = cache_mix(hash, out->catalog_bytes);
    hash = cache_mix(hash, out->package_bytes);
    hash = cache_mix(hash, out->source_count);
    hash = cache_mix(hash, out->sector_count);
    hash = cache_mix(hash, out->global_count);
    hash = cache_mix(hash, out->replica_count);
    hash = cache_mix(hash, out->material_count);
    hash = cache_mix(hash, out->texture_count);
    hash = cache_mix(hash, out->texture_bytes);
    hash = cache_mix(hash, cell_bits);
    hash = cache_mix_entry(hash, &entry);
    for (index = 0; index < catalog->sector_count; ++index) {
        if (!dc_render_entry_get(catalog, index, &entry)) return DC_RENDER_CACHE_ERR_ARGUMENTS;
        hash = cache_mix_entry(hash, &entry);
    }
    out->mix = hash;
    return NULL;
}

static int cache_identity_equal(const dc_render_identity *a, const dc_render_identity *b) {
    uint32_t a_bits, b_bits;
    memcpy(&a_bits, &a->cell_size, 4);
    memcpy(&b_bits, &b->cell_size, 4);
    return a->catalog_crc == b->catalog_crc && a->catalog_bytes == b->catalog_bytes &&
           a->package_bytes == b->package_bytes && a->source_count == b->source_count &&
           a->sector_count == b->sector_count && a->global_count == b->global_count &&
           a->replica_count == b->replica_count && a->material_count == b->material_count &&
           a->texture_count == b->texture_count && a->texture_bytes == b->texture_bytes &&
           a->mix == b->mix && a_bits == b_bits;
}

static void cache_identity_from_window(const dc_render_window *window, dc_render_identity *out) {
    memset(out, 0, sizeof(*out));
    out->catalog_crc = window->catalog_crc;
    out->catalog_bytes = window->catalog_bytes;
    out->package_bytes = window->package_bytes;
    out->source_count = window->source_count;
    out->sector_count = window->sector_count;
    out->global_count = window->global_count;
    out->replica_count = window->replica_count;
    out->material_count = window->material_count;
    out->texture_count = window->texture_count;
    out->texture_bytes = window->texture_bytes;
    out->mix = window->identity_mix;
    out->cell_size = window->cell_size;
}

static int cache_covered(const dc_render_query *coverage, const dc_render_query *query) {
    return coverage->x0 <= query->x0 && coverage->z0 <= query->z0 &&
           coverage->x1 >= query->x1 && coverage->z1 >= query->z1;
}

static int cache_unowned(const dc_render_window *window) {
    return !window->faces && !window->root && !window->source_sha &&
           window->count == 0 && window->valid == 0 && window->ready == 0;
}

static int cache_committed(const dc_render_window *window) {
    dc_render_identity ignored;
    if (window->valid != 1 || window->ready != 1) return 0;
    if (!cache_bounded_text(window->root, DC_RENDER_CACHE_ROOT_MAX + 1u)) return 0;
    if (!cache_bounded_text(window->source_sha, DC_RENDER_CACHE_SHA_MAX + 1u)) return 0;
    if (window->count > DC_RENDER_WINDOW_CAP) return 0;
    if (window->count == 0) {
        if (window->faces) return 0;
    } else if (!window->faces) return 0;
    if (!cache_query_ok(&window->coverage) || !cache_finite_cell(window->cell_size)) return 0;
    cache_identity_from_window(window, &ignored);
    return 1;
}

static char *cache_dup(const char *text, size_t max) {
    size_t length = strlen(text);
    char *copy;
    if (length == 0 || length > max) return NULL;
    copy = (char *)CACHE_MALLOC(length + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1u);
    return copy;
}

static const char *cache_load_block(const dc_render_catalog *catalog, const dc_render_entry *entry,
                                   int global, const char *path, dc_render_block *block) {
    FILE *file;
    const char *error;
    int closed;
    memset(block, 0, sizeof(*block));
    file = CACHE_FOPEN(path, "rb");
    if (!file) return DC_RENDER_CACHE_ERR_OPEN;
    error = dc_render_block_load(file, catalog, entry, global, block);
    closed = CACHE_FCLOSE(file);
    if (error) {
        dc_render_block_free(block);
        return error;
    }
    if (closed != 0) {
        dc_render_block_free(block);
        return DC_RENDER_CACHE_ERR_CLOSE;
    }
    return NULL;
}

static const char *cache_check_paths(const dc_render_job *job) {
    char path[DC_RENDER_CACHE_PATH_MAX];
    uint32_t index;
    const char *error = cache_path_global(path, job->root);
    if (error) return error;
    for (index = 0; index < job->ws->local_count; ++index) {
        error = cache_path_local(path, job->root, job->ws->locals[index].ix, job->ws->locals[index].iz);
        if (error) return error;
    }
    return NULL;
}

static uint32_t cache_unique_axis(const dc_render_entry *entries, uint32_t count, int use_z) {
    uint32_t i, unique = 0;
    for (i = 0; i < count; ++i) {
        uint32_t j;
        int32_t value = use_z ? entries[i].iz : entries[i].ix;
        int seen = 0;
        for (j = 0; j < i; ++j) {
            int32_t previous = use_z ? entries[j].iz : entries[j].ix;
            if (previous == value) { seen = 1; break; }
        }
        if (!seen) ++unique;
    }
    return unique;
}

static const char *cache_select_locals(dc_render_job *job) {
    uint32_t index;
    if (!dc_render_global_get(job->catalog, &job->ws->global_entry)) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    job->ws->local_count = 0;
    for (index = 0; index < job->catalog->sector_count; ++index) {
        dc_render_entry entry;
        double x0, x1, z0, z1;
        const char *error;
        if (!dc_render_entry_get(job->catalog, index, &entry)) return DC_RENDER_CACHE_ERR_ARGUMENTS;
        error = cache_cell_bounds(entry.ix, entry.iz, job->cell, &x0, &x1, &z0, &z1);
        if (error) return error;
        if (!cache_rect_touches(x0, x1, z0, z1, job->query)) continue;
        if (job->ws->local_count >= DC_RENDER_CACHE_LOCAL_CAP) return DC_RENDER_CACHE_ERR_COVERAGE;
        job->ws->locals[job->ws->local_count++] = entry;
    }
    if (cache_unique_axis(job->ws->locals, job->ws->local_count, 0) > DC_RENDER_CACHE_AXIS_CAP ||
        cache_unique_axis(job->ws->locals, job->ws->local_count, 1) > DC_RENDER_CACHE_AXIS_CAP)
        return DC_RENDER_CACHE_ERR_COVERAGE;
    return NULL;
}

static int cache_batch_find(const uint32_t *batch, uint32_t count, uint32_t id) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (batch[mid] < id) lo = mid + 1u;
        else hi = mid;
    }
    if (lo < count && batch[lo] == id) return (int)lo;
    return -1;
}

static const char *cache_merge_ids(dc_render_workspace *ws, uint32_t file_count) {
    uint32_t i = 0, j = 0, n = 0, k;
    while (n < DC_RENDER_WINDOW_CAP && (i < ws->batch_count || j < file_count)) {
        uint32_t pick;
        if (j >= file_count || (i < ws->batch_count && ws->batch[i] < ws->file_ids[j])) pick = ws->batch[i++];
        else if (i >= ws->batch_count || ws->file_ids[j] < ws->batch[i]) pick = ws->file_ids[j++];
        else {
            pick = ws->batch[i];
            ++i;
            ++j;
        }
        if (n && pick == ws->merged[n - 1u]) continue;
        ws->merged[n++] = pick;
    }
    if (n) memcpy(ws->batch, ws->merged, (size_t)n * sizeof(uint32_t));
    ws->batch_count = n;
    for (k = 1; k < n; ++k) if (ws->batch[k] <= ws->batch[k - 1u]) return DC_RENDER_CACHE_ERR_ORDER;
    return NULL;
}

static const char *cache_select_block(dc_render_job *job, const dc_render_block *block) {
    uint32_t i, count = 0, previous = 0;
    int have = 0;
    if (block->count > DC_RENDER_FILE_CAP) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    if (block->count && !block->records) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    for (i = 0; i < block->count; ++i) {
        uint32_t id = cache_u32(block->records + (size_t)i * DC_RENDER_FACE_BYTES);
        if (id >= job->source_count) return DC_RENDER_CACHE_ERR_ORDER;
        if (have && id <= previous) return DC_RENDER_CACHE_ERR_ORDER;
        previous = id;
        have = 1;
        if (id < job->cursor) continue;
        if (count >= DC_RENDER_FILE_CAP) return DC_RENDER_CACHE_ERR_ARGUMENTS;
        job->ws->file_ids[count++] = id;
    }
    return cache_merge_ids(job->ws, count);
}

static const char *cache_validate_block(dc_render_job *job, const dc_render_block *block, int global) {
    uint32_t i, previous = 0;
    int have = 0;
    if (block->count > DC_RENDER_FILE_CAP) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    if (block->count && !block->records) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    for (i = 0; i < block->count; ++i) {
        const unsigned char *record = block->records + (size_t)i * DC_RENDER_FACE_BYTES;
        uint32_t id = cache_u32(record);
        int slot;
        if (id >= job->source_count) return DC_RENDER_CACHE_ERR_ORDER;
        if (have && id <= previous) return DC_RENDER_CACHE_ERR_ORDER;
        previous = id;
        have = 1;
        if (id < job->cursor) continue;
        slot = cache_batch_find(job->ws->batch, job->ws->batch_count, id);
        if (slot < 0) {
            if (job->ws->batch_count < DC_RENDER_WINDOW_CAP ||
                id <= job->ws->batch[job->ws->batch_count - 1u])
                return DC_RENDER_CACHE_ERR_MISSING;
            continue;
        }
        if (!job->ws->seen[slot]) {
            int hit = cache_face_touches(record, job->query);
            if (hit < 0) return DC_RENDER_CACHE_ERR_REPLICA;
            memcpy(job->payloads + (size_t)slot * DC_RENDER_FACE_BYTES, record, DC_RENDER_FACE_BYTES);
            job->ws->seen[slot] = 1;
            job->ws->role[slot] = (uint8_t)(global ? ROLE_GLOBAL : ROLE_LOCAL);
            job->ws->included[slot] = (uint8_t)hit;
        } else if (memcmp(job->payloads + (size_t)slot * DC_RENDER_FACE_BYTES, record, DC_RENDER_FACE_BYTES) != 0) {
            return DC_RENDER_CACHE_ERR_REPLICA;
        } else if (global || job->ws->role[slot] == ROLE_GLOBAL) {
            return DC_RENDER_CACHE_ERR_ROLE;
        }
    }
    return NULL;
}

static const char *cache_stage_add(dc_render_job *job, const unsigned char *record) {
    if (job->stage_count >= DC_RENDER_WINDOW_CAP) return DC_RENDER_CACHE_ERR_FACE_CAP;
    if (job->stage_count == job->stage_cap) {
        uint32_t next_cap = job->stage_cap ? job->stage_cap << 1 : 16u;
        unsigned char *grown;
        if (next_cap > DC_RENDER_WINDOW_CAP) next_cap = DC_RENDER_WINDOW_CAP;
        grown = (unsigned char *)CACHE_MALLOC((size_t)next_cap * DC_RENDER_FACE_BYTES);
        if (!grown) return DC_RENDER_CACHE_ERR_ALLOCATION;
        if (job->stage_count)
            memcpy(grown, job->stage, (size_t)job->stage_count * DC_RENDER_FACE_BYTES);
        if (job->stage) CACHE_FREE(job->stage);
        job->stage = grown;
        job->stage_cap = next_cap;
    }
    memcpy(job->stage + (size_t)job->stage_count * DC_RENDER_FACE_BYTES, record, DC_RENDER_FACE_BYTES);
    job->stage_count += 1u;
    return NULL;
}

static const char *cache_run_pass(dc_render_job *job, int validate) {
    uint32_t index;
    for (index = 0; index <= job->ws->local_count; ++index) {
        dc_render_block block;
        const dc_render_entry *entry;
        char path[DC_RENDER_CACHE_PATH_MAX];
        const char *error;
        int global = index == 0;
        entry = global ? &job->ws->global_entry : &job->ws->locals[index - 1u];
        error = global ? cache_path_global(path, job->root)
                       : cache_path_local(path, job->root, entry->ix, entry->iz);
        if (error) return error;
        memset(&block,0,sizeof block);
        error = job->source ? job->source->load(job->source->user,job->catalog,entry,global,path,&block)
                            : cache_load_block(job->catalog, entry, global, path, &block);
        if (error) return error;
        if (block.count != entry->count) error = DC_RENDER_CACHE_ERR_ARGUMENTS;
        else if (validate) error = cache_validate_block(job, &block, global);
        else error = cache_select_block(job, &block);
        if(job->source) job->source->release(job->source->user,&block);
        else dc_render_block_free(&block);
        if (error) return error;
    }
    return NULL;
}

static const char *cache_finish_batch(dc_render_job *job) {
    uint32_t i, add = 0;
    for (i = 0; i < job->ws->batch_count; ++i) if (!job->ws->seen[i]) return DC_RENDER_CACHE_ERR_MISSING;
    for (i = 0; i < job->ws->batch_count; ++i) if (job->ws->included[i]) ++add;
    if (job->stage_count + add > DC_RENDER_WINDOW_CAP) return DC_RENDER_CACHE_ERR_FACE_CAP;
    for (i = 0; i < job->ws->batch_count; ++i) {
        const char *error;
        if (!job->ws->included[i]) continue;
        error = cache_stage_add(job, job->payloads + (size_t)i * DC_RENDER_FACE_BYTES);
        if (error) return error;
    }
    return NULL;
}

static void cache_install(dc_render_window *window, const dc_render_query *query,
                          const dc_render_identity *identity, unsigned char *faces,
                          uint32_t count, char *root, char *sha) {
    unsigned char *old_faces = window->faces;
    char *old_root = window->root;
    char *old_sha = window->source_sha;
    window->faces = faces;
    window->root = root;
    window->source_sha = sha;
    window->coverage = *query;
    window->count = count;
    window->catalog_crc = identity->catalog_crc;
    window->catalog_bytes = identity->catalog_bytes;
    window->package_bytes = identity->package_bytes;
    window->source_count = identity->source_count;
    window->sector_count = identity->sector_count;
    window->global_count = identity->global_count;
    window->replica_count = identity->replica_count;
    window->material_count = identity->material_count;
    window->texture_count = identity->texture_count;
    window->texture_bytes = identity->texture_bytes;
    window->identity_mix = identity->mix;
    window->cell_size = identity->cell_size;
    window->valid = 1;
    window->ready = 1;
    if (old_faces) CACHE_FREE(old_faces);
    if (old_root) CACHE_FREE(old_root);
    if (old_sha) CACHE_FREE(old_sha);
}

void dc_render_window_init(dc_render_window *window) {
    if (!window) return;
    if (!cache_unowned(window)) return;
    memset(window, 0, sizeof(*window));
}

void dc_render_window_free(dc_render_window *window) {
    if (!window) return;
    if (window->faces) CACHE_FREE(window->faces);
    if (window->root) CACHE_FREE(window->root);
    if (window->source_sha) CACHE_FREE(window->source_sha);
    memset(window, 0, sizeof(*window));
}

const char *dc_render_window_retarget_source(const dc_render_catalog *catalog, const char *root,
                                      const char *source_sha, const dc_render_query *query,
                                      const dc_render_block_source *source,dc_render_window *window) {
    char root_copy[DC_RENDER_CACHE_ROOT_MAX + 1u];
    char sha_copy[DC_RENDER_CACHE_SHA_MAX + 1u];
    dc_render_query query_copy;
    dc_render_identity identity, fresh;
    dc_render_job job;
    unsigned char *final_faces = NULL;
    char *new_root = NULL;
    char *new_sha = NULL;
    const char *error;
    memset(&job, 0, sizeof(job));
    if (!window) return DC_RENDER_CACHE_ERR_OUTPUT;
    if (!cache_unowned(window) && !cache_committed(window)) return DC_RENDER_CACHE_ERR_OWNED;
    if (!catalog || !catalog->storage || !root || !source_sha || !query) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    if(source&&(!source->load||!source->release))return DC_RENDER_CACHE_ERR_ARGUMENTS;
    if (!cache_sha_ok(source_sha,catalog)) return DC_RENDER_CACHE_ERR_SHA;
    if (!cache_root_ok(root)) return DC_RENDER_CACHE_ERR_ROOT;
    if (!cache_query_ok(query)) return DC_RENDER_CACHE_ERR_QUERY;
    if (!cache_finite_cell(catalog->cell_size)) return DC_RENDER_CACHE_ERR_ARGUMENTS;
    error = cache_capture(catalog, &identity);
    if (error) return error;
    memcpy(root_copy, root, strlen(root) + 1u);
    memcpy(sha_copy, source_sha, strlen(source_sha) + 1u);
    query_copy = *query;
    if (cache_committed(window)) {
        dc_render_identity held;
        cache_identity_from_window(window, &held);
        if (cache_identity_equal(&held, &identity) &&
            strcmp(window->root, root_copy) == 0 && strcmp(window->source_sha, sha_copy) == 0 &&
            cache_covered(&window->coverage, &query_copy))
            return NULL;
    }
    job.ws = (dc_render_workspace *)CACHE_MALLOC(sizeof(*job.ws));
    if (!job.ws) return DC_RENDER_CACHE_ERR_ALLOCATION;
    memset(job.ws, 0, sizeof(*job.ws));
    job.catalog = catalog;
    job.source = source;
    job.root = root_copy;
    job.query = &query_copy;
    job.cursor = 0;
    job.source_count = identity.source_count;
    job.cell = (double)identity.cell_size;
    error = cache_select_locals(&job);
    if (error) goto fail;
    error = cache_check_paths(&job);
    if (error) goto fail;
    for (;;) {
        uint32_t batch_count;
        job.ws->batch_count = 0;
        error = cache_run_pass(&job, 0);
        if (error) goto fail;
        batch_count = job.ws->batch_count;
        if (batch_count == 0) break;
        if (job.ws->batch[0] < job.cursor) {
            error = DC_RENDER_CACHE_ERR_COVERAGE;
            goto fail;
        }
        memset(job.ws->seen, 0, batch_count);
        memset(job.ws->role, 0, batch_count);
        memset(job.ws->included, 0, batch_count);
        job.payloads = (unsigned char *)CACHE_MALLOC((size_t)batch_count * DC_RENDER_FACE_BYTES);
        if (!job.payloads) {
            error = DC_RENDER_CACHE_ERR_ALLOCATION;
            goto fail;
        }
        error = cache_run_pass(&job, 1);
        if (error) goto fail;
        error = cache_finish_batch(&job);
        if (error) goto fail;
        CACHE_FREE(job.payloads);
        job.payloads = NULL;
        if (batch_count < DC_RENDER_WINDOW_CAP) break;
        if (job.ws->batch[batch_count - 1u] == UINT32_MAX) {
            error = DC_RENDER_CACHE_ERR_COVERAGE;
            goto fail;
        }
        job.cursor = job.ws->batch[batch_count - 1u] + 1u;
    }
    error = cache_capture(catalog, &fresh);
    if (error) goto fail;
    if (!cache_identity_equal(&fresh, &identity)) {
        error = DC_RENDER_CACHE_ERR_IDENTITY;
        goto fail;
    }
    if (job.stage_count) {
        final_faces = (unsigned char *)CACHE_MALLOC((size_t)job.stage_count * DC_RENDER_FACE_BYTES);
        if (!final_faces) {
            error = DC_RENDER_CACHE_ERR_ALLOCATION;
            goto fail;
        }
        memcpy(final_faces, job.stage, (size_t)job.stage_count * DC_RENDER_FACE_BYTES);
    }
    new_root = cache_dup(root_copy, DC_RENDER_CACHE_ROOT_MAX);
    if (!new_root) {
        error = DC_RENDER_CACHE_ERR_ALLOCATION;
        goto fail;
    }
    new_sha = cache_dup(sha_copy, DC_RENDER_CACHE_SHA_MAX);
    if (!new_sha) {
        error = DC_RENDER_CACHE_ERR_ALLOCATION;
        goto fail;
    }
    cache_install(window, &query_copy, &identity, final_faces, job.stage_count, new_root, new_sha);
    if (job.stage) CACHE_FREE(job.stage);
    if (job.payloads) CACHE_FREE(job.payloads);
    CACHE_FREE(job.ws);
    return NULL;
fail:
    if (final_faces) CACHE_FREE(final_faces);
    if (new_root) CACHE_FREE(new_root);
    if (new_sha) CACHE_FREE(new_sha);
    if (job.stage) CACHE_FREE(job.stage);
    if (job.payloads) CACHE_FREE(job.payloads);
    if (job.ws) CACHE_FREE(job.ws);
    return error;
}

const char *dc_render_window_retarget(const dc_render_catalog *catalog,const char *root,
    const char *source_sha,const dc_render_query *query,dc_render_window *window) {
    return dc_render_window_retarget_source(catalog,root,source_sha,query,NULL,window);
}
