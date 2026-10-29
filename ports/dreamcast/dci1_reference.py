"""Independent DCI1 parser and pixel-centre reference rasterizer. (AI-assisted), ISC."""

import argparse
import json
import math
from pathlib import Path
import struct


HEADER = struct.Struct('<4sII')
PROTOTYPE = struct.Struct('<5I32s')
VERTEX = struct.Struct('<5fI')
INSTANCE = struct.Struct('<2H12f')
WIDTH, HEIGHT = 640, 480
NEAR = 0.05


def parse(data):
    """Return prototype, vertex, index and instance lists after strict validation.

    Vertex records retain UV and ARGB fields; rendering uses vertex RGB only.
    Instances are (prototype_index, matrix_tuple). No game assets are required.
    """
    if len(data) < HEADER.size:
        raise ValueError('truncated header')
    magic, np, ni = HEADER.unpack_from(data)
    if magic != b'DCI1':
        raise ValueError('invalid magic')
    end = HEADER.size + np * PROTOTYPE.size
    if end > len(data):
        raise ValueError('truncated prototype table')
    prototypes = []
    nv = nx = 0
    for offset in range(HEADER.size, end, PROTOTYPE.size):
        vc, ic, fv, fi, material, raw_name = PROTOTYPE.unpack_from(data, offset)
        if fv != nv:
            raise ValueError('noncontiguous first_vertex')
        if fi != nx:
            raise ValueError('noncontiguous first_index')
        if ic % 3:
            raise ValueError('index_count is not divisible by three')
        try:
            name = raw_name.split(b'\0', 1)[0].decode('ascii')
        except UnicodeDecodeError as exc:
            raise ValueError('non-ASCII prototype name') from exc
        prototypes.append(dict(vertex_count=vc, index_count=ic,
                               first_vertex=fv, first_index=fi,
                               material=material, name=name))
        nv += vc
        nx += ic
    vertex_end = end + nv * VERTEX.size
    index_end = vertex_end + nx * 2
    padded_end = (index_end + 3) & ~3
    if padded_end + ni * INSTANCE.size != len(data):
        raise ValueError('incorrect total size')
    vertices = list(VERTEX.iter_unpack(data[end:vertex_end]))
    if any(not math.isfinite(v) for vertex in vertices for v in vertex[:5]):
        raise ValueError('nonfinite vertex float')
    indices = [item[0] for item in struct.iter_unpack('<H', data[vertex_end:index_end])]
    if any(data[index_end:padded_end]):
        raise ValueError('nonzero index padding')
    for proto in prototypes:
        start = proto['first_index']
        if any(i >= proto['vertex_count']
               for i in indices[start:start + proto['index_count']]):
            raise ValueError('index outside prototype')
    instances = []
    for record in INSTANCE.iter_unpack(data[padded_end:]):
        if record[0] >= np:
            raise ValueError('instance prototype out of range')
        if any(not math.isfinite(v) for v in record[2:]):
            raise ValueError('nonfinite instance float')
        instances.append((record[0], record[2:]))
    return dict(prototypes=prototypes, vertices=vertices,
                indices=indices, instances=instances)


def float32(value):
    """Round one operation to IEEE binary32, including overflow to infinity."""
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def within_draw_distance(matrix, camera):
    dx = float32(matrix[9] - camera[0])
    dz = float32(matrix[11] - camera[2])
    return (float32(float32(dx * dx) + float32(dz * dz))
            <= float32(camera[6] * camera[6]))


def view_point(point, camera):
    tx, ty, tz, yaw, pitch, distance, _ = camera
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    x, y, z = (point[0] - tx, point[1] - ty, point[2] - tz)
    return (0.25 * (cy * x + sy * z),
            0.25 * (sp * sy * x + cp * y - sp * cy * z),
            0.25 * (-cp * sy * x + sp * y + cp * cy * z + distance))


def clip_near(vertices):
    """Clip (x,y,z,r,g,b) vertices; channel interpolation is in view space."""
    result = []
    if not vertices:
        return result
    previous = vertices[-1]
    for current in vertices:
        was_inside, inside = previous[2] >= NEAR, current[2] >= NEAR
        if inside != was_inside:
            t = (NEAR - previous[2]) / (current[2] - previous[2])
            cut = tuple(a + t * (b - a) for a, b in zip(previous, current))
            result.append(cut[:2] + (NEAR,) + cut[3:])
        if inside:
            result.append(current)
        previous = current
    return result


def project(vertex):
    x, y, z = vertex[:3]
    w = 1.0 / z
    return (320 + 390 * x * w, 260 - 390 * y * w, w,
            *(channel * w for channel in vertex[3:6]))


def _edge(a, b, x, y):
    return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])


def _top_left(a, b):
    # Positive area in screen coordinates: upward edges and rightward top edges.
    dy, dx = b[1] - a[1], b[0] - a[0]
    return dy < 0 or (dy == 0 and dx > 0)


def _rasterize(triangle, rgb, depth, stats):
    a, b, c = map(project, triangle)
    area = _edge(a, b, c[0], c[1])
    if area == 0:
        return
    if area < 0:
        b, c = c, b
        area = -area
    xmin = max(0, math.ceil(min(a[0], b[0], c[0]) - 0.5))
    xmax = min(WIDTH - 1, math.floor(max(a[0], b[0], c[0]) - 0.5))
    ymin = max(0, math.ceil(min(a[1], b[1], c[1]) - 0.5))
    ymax = min(HEIGHT - 1, math.floor(max(a[1], b[1], c[1]) - 0.5))
    if xmin > xmax or ymin > ymax:
        return
    stats['triangles_rasterized'] += 1
    edges = ((b, c), (c, a), (a, b))
    row = [_edge(p, q, xmin + 0.5, ymin + 0.5) for p, q in edges]
    step_x = [p[1] - q[1] for p, q in edges]
    step_y = [q[0] - p[0] for p, q in edges]
    tl0, tl1, tl2 = [_top_left(p, q) for p, q in edges]
    inverse_area = 1.0 / area
    for y in range(ymin, ymax + 1):
        e0, e1, e2 = row
        index = y * WIDTH + xmin
        for _ in range(xmin, xmax + 1):
            if ((e0 > 0 or (e0 == 0 and tl0))
                    and (e1 > 0 or (e1 == 0 and tl1))
                    and (e2 > 0 or (e2 == 0 and tl2))):
                stats['pixels_covered'] += 1
                w = (e0 * a[2] + e1 * b[2] + e2 * c[2]) * inverse_area
                if w > depth[index]:
                    depth[index] = w
                    factor = inverse_area / w
                    pixel = index * 3
                    for channel in range(3):
                        value = ((e0 * a[channel + 3] + e1 * b[channel + 3]
                                  + e2 * c[channel + 3]) * factor)
                        rgb[pixel + channel] = min(255, max(0, int(value + 0.5)))
            e0 += step_x[0]
            e1 += step_x[1]
            e2 += step_x[2]
            index += 1
        for i in range(3):
            row[i] += step_y[i]


def render(package_bytes, camera, background=(115, 158, 204)):
    """Render at 640x480; coverage counts fragments before the depth test.

    Visited triangles belong to drawn instances. Near-clipped counts original
    triangles having any vertex outside, including wholly rejected triangles.
    Rasterized counts nondegenerate projected fan triangles with nonempty boxes.
    Z ties preserve the earlier triangle. Geometry uses Python binary64 except
    the explicitly binary32 draw-distance operations.
    """
    if len(camera) != 7 or not all(math.isfinite(v) for v in camera):
        raise ValueError('camera requires seven finite values')
    package = parse(package_bytes)
    stats = dict.fromkeys(('instances_visited', 'instances_drawn',
                           'triangles_visited', 'triangles_near_clipped',
                           'triangles_rasterized', 'pixels_covered'), 0)
    rgb = bytearray(bytes(background) * (WIDTH * HEIGHT))
    depth = [0.0] * (WIDTH * HEIGHT)
    for prototype_index, m in package['instances']:
        stats['instances_visited'] += 1
        if not within_draw_distance(m, camera):
            continue
        stats['instances_drawn'] += 1
        proto = package['prototypes'][prototype_index]
        transformed = []
        start = proto['first_vertex']
        for vertex in package['vertices'][start:start + proto['vertex_count']]:
            x, y, z = vertex[:3]
            world = tuple(x * m[i] + y * m[3 + i] + z * m[6 + i] + m[9 + i]
                          for i in range(3))
            argb = vertex[5]
            transformed.append(view_point(world, camera)
                               + ((argb >> 16) & 255, (argb >> 8) & 255, argb & 255))
        start = proto['first_index']
        for offset in range(start, start + proto['index_count'], 3):
            stats['triangles_visited'] += 1
            triangle = [transformed[i] for i in package['indices'][offset:offset + 3]]
            if any(v[2] < NEAR for v in triangle):
                stats['triangles_near_clipped'] += 1
                triangle = clip_near(triangle)
            for i in range(1, len(triangle) - 1):
                _rasterize((triangle[0], triangle[i], triangle[i + 1]), rgb, depth, stats)
    return WIDTH, HEIGHT, rgb, stats


def write_ppm(path, w, h, rgb):
    if w <= 0 or h <= 0 or len(rgb) != w * h * 3:
        raise ValueError('invalid PPM dimensions or pixel count')
    Path(path).write_bytes(f'P6\n{w} {h}\n255\n'.encode('ascii') + bytes(rgb))


def read_ppm(path):
    """Read P6 with comments/whitespace between header tokens.

    The declared raster length separates trailing header whitespace/comments
    from binary pixels. Pixel bytes may themselves be whitespace or '#'.
    """
    data = Path(path).read_bytes()
    pos = 0
    tokens = []
    whitespace = b' \t\r\n\v\f'
    for _ in range(4):
        while pos < len(data):
            if data[pos] in whitespace:
                pos += 1
            elif data[pos] == 35:
                end = data.find(b'\n', pos)
                pos = len(data) if end < 0 else end + 1
            else:
                break
        start = pos
        while pos < len(data) and data[pos] not in whitespace and data[pos] != 35:
            pos += 1
        tokens.append(data[start:pos])
    try:
        w, h, maxval = map(int, tokens[1:])
    except ValueError as exc:
        raise ValueError('invalid PPM header') from exc
    if tokens[0] != b'P6' or maxval != 255 or w <= 0 or h <= 0:
        raise ValueError('unsupported PPM header')
    if pos >= len(data) or data[pos] not in whitespace:
        raise ValueError('missing PPM raster separator')
    raster = len(data) - w * h * 3
    if raster <= pos:
        raise ValueError('incorrect PPM pixel count')
    while pos < raster:
        if data[pos] in whitespace:
            pos += 1
        elif data[pos] == 35:
            end = data.find(b'\n', pos, raster)
            if end < 0:
                raise ValueError('unterminated PPM header comment')
            pos = end + 1
        else:
            raise ValueError('incorrect PPM pixel count')
    return w, h, bytearray(data[raster:])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--camera', required=True,
                        help='tx,ty,tz,yaw,pitch,distance,draw (metres/radians)')
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--json', type=Path, help='write render statistics')
    args = parser.parse_args(argv)
    try:
        camera = tuple(map(float, args.camera.split(',')))
        w, h, rgb, stats = render(args.package.read_bytes(), camera)
        write_ppm(args.out, w, h, rgb)
        if args.json:
            args.json.write_text(json.dumps(stats, indent=2) + '\n', encoding='utf-8')
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
