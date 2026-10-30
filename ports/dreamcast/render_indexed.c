/* Validated, lossless DRX1 sector expansion without allocation. (AI-assisted), ISC. */
#include "render_indexed.h"

#include <stddef.h>
#include <string.h>

static uint32_t read_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_u16(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

/* zlib's CRC-32, a nibble at a time: a 64-byte table, a quarter of the
 * bitwise loop's iterations. Every cache hit checks a whole block. */
static const uint32_t crc_nibble[16] = {
    UINT32_C(0x00000000), UINT32_C(0x1db71064), UINT32_C(0x3b6e20c8), UINT32_C(0x26d930ac),
    UINT32_C(0x76dc4190), UINT32_C(0x6b6b51f4), UINT32_C(0x4db26158), UINT32_C(0x5005713c),
    UINT32_C(0xedb88320), UINT32_C(0xf00f9344), UINT32_C(0xd6d6a3e8), UINT32_C(0xcb61b38c),
    UINT32_C(0x9b64c2b0), UINT32_C(0x86d3d2d4), UINT32_C(0xa00ae278), UINT32_C(0xbdbdf21c)};

static uint32_t crc32_bytes(const unsigned char *p, uint32_t bytes) {
    uint32_t crc = UINT32_C(0xffffffff);
    uint32_t i;
    for (i = 0; i < bytes; ++i) {
        crc ^= p[i];
        crc = (crc >> 4) ^ crc_nibble[crc & 15u];
        crc = (crc >> 4) ^ crc_nibble[crc & 15u];
    }
    return crc ^ UINT32_C(0xffffffff);
}

static void write_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static int overlap(const void *a, size_t na, const void *b, size_t nb) {
    uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
    if (!na || !nb) return 0;
    return aa <= bb ? bb - aa < na : aa - bb < nb;
}

const char *dc_render_indexed_decode(const void *blob, uint32_t bytes,
                                    void *out_faces, uint32_t out_capacity_faces,
                                    uint32_t *out_count) {
    const unsigned char *input = (const unsigned char *)blob;
    unsigned char *output = (unsigned char *)out_faces;
    uint32_t nv, nf, i, face_offset;
    uint64_t expected, output_bytes;
    if (!out_count) return "null count";
    if (!input) return "null blob";
    if (bytes < 16u) return "truncated header";
    if (memcmp(input, "DRX1", 4) != 0) return "bad magic";
    nv = read_u32(input + 4);
    nf = read_u32(input + 8);
    if (nv > 65535u) return "too many vertices";
    expected = UINT64_C(16) + (uint64_t)nv * 36u + (uint64_t)nf * 16u;
    if (expected != bytes) return "incorrect size";
    if (crc32_bytes(input, bytes - 4u) != read_u32(input + bytes - 4u))
        return "CRC mismatch";
    if (nf > out_capacity_faces) return "output capacity";
    if (nf && !output) return "null output";
    output_bytes = (uint64_t)nf * 116u;
    if (output_bytes > SIZE_MAX) return "output size";
    if (overlap(input, bytes, output, (size_t)output_bytes) ||
        overlap(input, bytes, out_count, sizeof(*out_count)) ||
        overlap(output, (size_t)output_bytes, out_count, sizeof(*out_count)))
        return "overlapping buffers";
    /* Inspect IEEE-754 exponent bits directly: no host float layout, alignment,
     * NaN conversion or floating-point mode can alter the preserved bytes. */
    for (i = 0; i < nv; ++i) {
        unsigned channel;
        const unsigned char *v = input + 12u + i * 36u;
        for (channel = 0; channel < 8; ++channel) {
            if ((read_u32(v + channel * 4u) & UINT32_C(0x7f800000)) ==
                UINT32_C(0x7f800000)) return "nonfinite vertex";
        }
    }
    face_offset = 12u + nv * 36u;
    for (i = 0; i < nf; ++i) {
        const unsigned char *f = input + face_offset + i * 16u;
        if (read_u16(f + 14) != 0u) return "nonzero pad";
        if (read_u16(f + 8) >= nv || read_u16(f + 10) >= nv ||
            read_u16(f + 12) >= nv) return "index out of range";
    }
    /* Validation is complete. Copy raw little-endian fields without repacking. */
    for (i = 0; i < nf; ++i) {
        const unsigned char *f = input + face_offset + i * 16u;
        unsigned char *dest = output + (size_t)i * 116u;
        unsigned vertex;
        memcpy(dest, f, 8);
        for (vertex = 0; vertex < 3; ++vertex) {
            uint32_t index = read_u16(f + 8u + vertex * 2u);
            memcpy(dest + 8u + vertex * 36u, input + 12u + index * 36u, 36);
        }
    }
    *out_count = nf;
    return NULL;
}

static uint32_t table_size(uint32_t faces) {
    uint32_t size = 1;
    while (size < faces * 6u) size <<= 1;
    return size;
}

uint32_t dc_render_indexed_scratch_bytes(uint32_t face_count) {
    if (face_count > DC_RENDER_INDEXED_MAX_FACES) return 0;
    return table_size(face_count ? face_count : 1u) * 6u;
}

static uint32_t hash_vertex(const unsigned char *v) {
    uint32_t h = UINT32_C(2166136261);
    unsigned i;
    for (i = 0; i < 36; ++i) h = (h ^ v[i]) * UINT32_C(16777619);
    return h;
}

/* First-use order over all 36 vertex bytes, as indexed_sectors.encode_sector.
 * keys holds face-vertex positions plus one (0 = empty); ids the vertex index
 * each key was given. Returns the unique count, or UINT32_MAX past the cap. */
static uint32_t share(const unsigned char *faces, uint32_t count, uint32_t *keys,
                      uint16_t *ids, uint32_t mask, unsigned char *vertices,
                      unsigned char *indices) {
    uint32_t unique = 0, i;
    memset(keys, 0, (mask + 1u) * sizeof(*keys));
    for (i = 0; i < count * 3u; ++i) {
        const unsigned char *v = faces + (i / 3u) * 116u + 8u + (i % 3u) * 36u;
        uint32_t slot = hash_vertex(v) & mask, index;
        for (;;) {
            uint32_t key = keys[slot];
            if (!key) {
                if (unique >= DC_RENDER_INDEXED_MAX_VERTICES) return UINT32_MAX;
                keys[slot] = i + 1u;
                ids[slot] = (uint16_t)unique;
                if (vertices) memcpy(vertices + unique * 36u, v, 36);
                index = unique++;
                break;
            }
            --key;
            if (!memcmp(faces + (key / 3u) * 116u + 8u + (key % 3u) * 36u, v, 36)) {
                index = ids[slot];
                break;
            }
            slot = (slot + 1u) & mask;
        }
        if (indices) {
            unsigned char *f = indices + (i / 3u) * 16u;
            f[8u + (i % 3u) * 2u] = (unsigned char)index;
            f[9u + (i % 3u) * 2u] = (unsigned char)(index >> 8);
        }
    }
    return unique;
}

const char *dc_render_indexed_encode(const void *faces, uint32_t face_count,
                                    void *scratch, uint32_t scratch_bytes,
                                    void *out, uint32_t capacity, uint32_t *out_bytes) {
    const unsigned char *input = (const unsigned char *)faces;
    unsigned char *output = (unsigned char *)out;
    uint32_t size, unique, i, bytes;
    uint32_t *keys;
    uint16_t *ids;
    if (!out_bytes) return "null size";
    if (face_count > DC_RENDER_INDEXED_MAX_FACES) return "too many faces";
    if (face_count && !input) return "null faces";
    size = table_size(face_count ? face_count : 1u);
    if (!scratch || scratch_bytes < size * 6u ||
        ((uintptr_t)scratch & (sizeof(uint32_t) - 1u))) return "scratch";
    if (overlap(input, (size_t)face_count * 116u, scratch, scratch_bytes) ||
        overlap(scratch, scratch_bytes, out_bytes, sizeof(*out_bytes)))
        return "overlapping buffers";
    keys = (uint32_t *)scratch;
    ids = (uint16_t *)(keys + size);
    for (i = 0; i < face_count * 3u; ++i) {
        const unsigned char *v = input + (i / 3u) * 116u + 8u + (i % 3u) * 36u;
        unsigned channel;
        for (channel = 0; channel < 8; ++channel) {
            if ((read_u32(v + channel * 4u) & UINT32_C(0x7f800000)) ==
                UINT32_C(0x7f800000)) return "nonfinite vertex";
        }
    }
    unique = share(input, face_count, keys, ids, size - 1u, NULL, NULL);
    if (unique == UINT32_MAX) return "too many vertices";
    bytes = 16u + unique * 36u + face_count * 16u;
    if (!output) {
        if (capacity) return "null output";
        *out_bytes = bytes;
        return NULL;
    }
    if (capacity < bytes) return "output capacity";
    if (overlap(input, (size_t)face_count * 116u, output, bytes) ||
        overlap(scratch, scratch_bytes, output, bytes) ||
        overlap(output, bytes, out_bytes, sizeof(*out_bytes)))
        return "overlapping buffers";
    memcpy(output, "DRX1", 4);
    write_u32(output + 4, unique);
    write_u32(output + 8, face_count);
    for (i = 0; i < face_count; ++i) {
        unsigned char *f = output + 12u + unique * 36u + i * 16u;
        memcpy(f, input + i * 116u, 8);
        f[14] = f[15] = 0;
    }
    share(input, face_count, keys, ids, size - 1u, output + 12u,
          output + 12u + unique * 36u);
    write_u32(output + bytes - 4u, crc32_bytes(output, bytes - 4u));
    *out_bytes = bytes;
    return NULL;
}
