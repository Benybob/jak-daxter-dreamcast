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
static const char *decode(const void *blob, uint32_t bytes, void *out_faces,
                          uint32_t out_capacity_faces, uint32_t *out_count,
                          const char *magic, uint32_t stored) {
    const unsigned char *input = (const unsigned char *)blob;
    unsigned char *output = (unsigned char *)out_faces;
    uint32_t nv, nf, i, face_offset;
    uint64_t expected, output_bytes;
    if (!out_count) return "null count";
    if (!input) return "null blob";
    if (bytes < 16u) return "truncated header";
    if (memcmp(input, magic, 4) != 0) return "bad magic";
    nv = read_u32(input + 4);
    nf = read_u32(input + 8);
    if (nv > 65535u) return "too many vertices";
    expected = UINT64_C(16) + (uint64_t)nv * stored + (uint64_t)nf * 16u;
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
        const unsigned char *v = input + 12u + i * stored;
        for (channel = 0; channel < (stored - 4u) / 4u; ++channel) {
            if ((read_u32(v + channel * 4u) & UINT32_C(0x7f800000)) ==
                UINT32_C(0x7f800000)) return "nonfinite vertex";
        }
    }
    face_offset = 12u + nv * stored;
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
