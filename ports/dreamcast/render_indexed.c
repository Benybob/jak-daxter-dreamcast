/* Validated DRX1 (lossless) and DRX2 (draw-only) expansion without allocation. (AI-assisted), ISC. */
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

static uint32_t crc32_bytes(const unsigned char *p, uint32_t bytes) {
    uint32_t crc = UINT32_C(0xffffffff);
    uint32_t i;
    for (i = 0; i < bytes; ++i) {
        unsigned bit;
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static int overlap(const void *a, size_t na, const void *b, size_t nb) {
    uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
    if (!na || !nb) return 0;
    return aa <= bb ? bb - aa < na : aa - bb < nb;
}

/* Stored vertices are 36 bytes (DRX1) or 24, which omit the normal at +20..+31. */
static void expand_face(const unsigned char *input, const unsigned char *f, uint32_t stored,
                        unsigned char *dest) {
    unsigned vertex;
    memcpy(dest, f, 8);
    for (vertex = 0; vertex < 3; ++vertex) {
        uint32_t index = read_u16(f + 8u + vertex * 2u);
        const unsigned char *v = input + 12u + index * stored;
        unsigned char *to = dest + 8u + vertex * 36u;
        if (stored == 36u) {
            memcpy(to, v, 36);
        } else {
            memcpy(to, v, 20);
            memset(to + 20, 0, 12);
            memcpy(to + 32, v + 20, 4);
        }
    }
}

static void expand(const unsigned char *input, uint32_t nv, uint32_t nf, uint32_t stored,
                   unsigned char *output) {
    uint32_t i, face_offset = 12u + nv * stored;
    for (i = 0; i < nf; ++i)
        expand_face(input, input + face_offset + i * 16u, stored, output + (size_t)i * 116u);
}

static const char *check_header(const unsigned char *input, uint32_t bytes, const char *magic,
                                uint32_t stored, uint32_t *nv, uint32_t *nf) {
    uint64_t expected;
    if (!input) return "null blob";
    if (bytes < 16u) return "truncated header";
    if (memcmp(input, magic, 4) != 0) return "bad magic";
    *nv = read_u32(input + 4);
    *nf = read_u32(input + 8);
    if (*nv > 65535u) return "too many vertices";
    expected = UINT64_C(16) + (uint64_t)*nv * stored + (uint64_t)*nf * 16u;
    if (expected != bytes) return "incorrect size";
    if (crc32_bytes(input, bytes - 4u) != read_u32(input + bytes - 4u))
        return "CRC mismatch";
    return NULL;
}

static const char *check_body(const unsigned char *input, uint32_t nv, uint32_t nf, uint32_t stored) {
    uint32_t i, face_offset = 12u + nv * stored;
    /* Inspect IEEE-754 exponent bits directly: no host float layout, alignment,
     * NaN conversion or floating-point mode can alter the preserved bytes. */
    for (i = 0; i < nv; ++i) {
        unsigned channel;
        const unsigned char *v = input + 12u + i * stored;
        for (channel = 0; channel < (stored - 4u) / 4u; ++channel) {
            if ((read_u32(v + channel * 4u) & UINT32_C(0x7f800000)) ==
                UINT32_C(0x7f800000)) return "nonfinite vertex";
        }
    }
    for (i = 0; i < nf; ++i) {
        const unsigned char *f = input + face_offset + i * 16u;
        if (read_u16(f + 14) != 0u) return "nonzero pad";
        if (read_u16(f + 8) >= nv || read_u16(f + 10) >= nv ||
            read_u16(f + 12) >= nv) return "index out of range";
    }
    return NULL;
}

static const char *decode(const void *blob, uint32_t bytes, void *out_faces,
                          uint32_t out_capacity_faces, uint32_t *out_count,
                          const char *magic, uint32_t stored) {
    const unsigned char *input = (const unsigned char *)blob;
    unsigned char *output = (unsigned char *)out_faces;
    uint32_t nv, nf;
    uint64_t output_bytes;
    const char *error;
    if (!out_count) return "null count";
    error = check_header(input, bytes, magic, stored, &nv, &nf);
    if (error) return error;
    if (nf > out_capacity_faces) return "output capacity";
    if (nf && !output) return "null output";
    output_bytes = (uint64_t)nf * 116u;
    if (output_bytes > SIZE_MAX) return "output size";
    if (overlap(input, bytes, output, (size_t)output_bytes) ||
        overlap(input, bytes, out_count, sizeof(*out_count)) ||
        overlap(output, (size_t)output_bytes, out_count, sizeof(*out_count)))
        return "overlapping buffers";
    error = check_body(input, nv, nf, stored);
    if (error) return error;
    /* Validation is complete. Copy raw little-endian fields without repacking. */
    expand(input, nv, nf, stored, output);
    *out_count = nf;
    return NULL;
}

const char *dc_render_indexed_decode(const void *blob, uint32_t bytes,
                                    void *out_faces, uint32_t out_capacity_faces,
                                    uint32_t *out_count) {
    return decode(blob, bytes, out_faces, out_capacity_faces, out_count, "DRX1", 36u);
}

const char *dc_render_indexed_decode_draw(const void *blob, uint32_t bytes,
                                         void *out_faces, uint32_t out_capacity_faces,
                                         uint32_t *out_count) {
    return decode(blob, bytes, out_faces, out_capacity_faces, out_count, "DRX2", 24u);
}

const char *dc_render_indexed_check_draw(const void *blob, uint32_t bytes, uint32_t *out_count) {
    const unsigned char *input = (const unsigned char *)blob;
    uint32_t nv, nf;
    const char *error;
    if (!out_count) return "null count";
    error = check_header(input, bytes, "DRX2", 24u, &nv, &nf);
    if (!error) error = check_body(input, nv, nf, 24u);
    if (!error) *out_count = nf;
    return error;
}

void dc_render_indexed_expand_draw(const void *blob, void *out_faces) {
    const unsigned char *input = (const unsigned char *)blob;
    expand(input, read_u32(input + 4), read_u32(input + 8), 24u, (unsigned char *)out_faces);
}

void dc_render_indexed_expand_draw_face(const void *blob, uint32_t index, void *out_face) {
    const unsigned char *input = (const unsigned char *)blob;
    expand_face(input, input + 12u + read_u32(input + 4) * 24u + index * 16u, 24u,
                (unsigned char *)out_face);
}

uint32_t dc_render_indexed_draw_bound(uint32_t faces) {
    return 16u + faces * (3u * 24u + 16u);
}

static void put_u32(unsigned char *p, uint32_t value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

const char *dc_render_indexed_encode_draw(const void *faces, uint32_t count, void *out,
                                         uint32_t capacity, uint16_t *table,
                                         uint32_t table_slots, uint32_t *out_bytes) {
    const unsigned char *input = (const unsigned char *)faces;
    unsigned char *output = (unsigned char *)out;
    unsigned char *staged;
    uint32_t i, nv = 0, bytes;
    if (!out_bytes || !output || !table) return "null argument";
    if (count && !input) return "null faces";
    if (count > DC_RENDER_INDEXED_DRAW_MAX_FACES) return "too many faces";
    if (capacity < dc_render_indexed_draw_bound(count)) return "output capacity";
    if (table_slots < 8u || table_slots < 6u * count || (table_slots & (table_slots - 1u)))
        return "table size";
    memset(table, 0, (size_t)table_slots * sizeof(*table));
    /* Faces wait above the largest vertex run, then close the gap. */
    staged = output + 12u + count * 72u;
    for (i = 0; i < count; ++i) {
        const unsigned char *record = input + (size_t)i * 116u;
        unsigned char *f = staged + i * 16u;
        unsigned vertex;
        memcpy(f, record, 8);
        f[14] = f[15] = 0;
        for (vertex = 0; vertex < 3; ++vertex) {
            const unsigned char *v = record + 8u + vertex * 36u;
            unsigned char key[24];
            uint32_t hash = UINT32_C(2166136261), slot, channel, index = 0;
            unsigned b;
            for (channel = 0; channel < 8; ++channel) {
                if ((read_u32(v + channel * 4u) & UINT32_C(0x7f800000)) ==
                    UINT32_C(0x7f800000)) return "nonfinite vertex";
            }
            memcpy(key, v, 20);
            memcpy(key + 20, v + 32, 4);
            for (b = 0; b < 24; ++b) hash = (hash ^ key[b]) * UINT32_C(16777619);
            for (slot = hash & (table_slots - 1u);; slot = (slot + 1u) & (table_slots - 1u)) {
                if (!table[slot]) {
                    if (nv >= 65535u) return "too many vertices";
                    index = nv++;
                    table[slot] = (uint16_t)(index + 1u);
                    memcpy(output + 12u + index * 24u, key, 24);
                    break;
                }
                index = table[slot] - 1u;
                if (!memcmp(output + 12u + index * 24u, key, 24)) break;
            }
            f[8u + vertex * 2u] = (unsigned char)index;
            f[9u + vertex * 2u] = (unsigned char)(index >> 8);
        }
    }
    memmove(output + 12u + nv * 24u, staged, (size_t)count * 16u);
    memcpy(output, "DRX2", 4);
    put_u32(output + 4, nv);
    put_u32(output + 8, count);
    bytes = 12u + nv * 24u + count * 16u;
    put_u32(output + bytes, crc32_bytes(output, bytes));
    *out_bytes = bytes + 4u;
    return NULL;
}
