"""Compare P6 framebuffer coverage and RGB error with a visual diff. (AI-assisted), ISC."""

import argparse
from collections import deque
import json
from pathlib import Path

from dci1_reference import read_ppm, write_ppm


def quantize565(rgb):
    """Round to nearest RGB565 level and expand with bit replication.

    Integer half-up rounding chooses the code; expansion mirrors conventional
    RGB565 captures. Already expanded values are unchanged (idempotent).
    """
    result = bytearray(len(rgb))
    for i, value in enumerate(rgb):
        if i % 3 == 1:
            code = (value * 63 + 127) // 255
            result[i] = (code << 2) | (code >> 4)
        else:
            code = (value * 31 + 127) // 255
            result[i] = (code << 3) | (code >> 2)
    return result


def _classify(a, b, background, tolerance):
    return bytearray(
        (sum(abs(a[i + c] - background[c]) for c in range(3)) > tolerance)
        | ((sum(abs(b[i + c] - background[c]) for c in range(3)) > tolerance) << 1)
        for i in range(0, len(a), 3))


def _largest_region(classes, w, h):
    """Return inclusive [xmin,ymin,xmax,ymax] for largest coverage XOR region.

    Equal-sized regions retain the first in row-major order; no region is None.
    """
    seen = bytearray(w * h)
    largest, bounds = 0, None
    for start, kind in enumerate(classes):
        if kind not in (1, 2) or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        count = 0
        xmin = xmax = start % w
        ymin = ymax = start // w
        while queue:
            index = queue.popleft()
            x, y = index % w, index // w
            xmin, xmax = min(xmin, x), max(xmax, x)
            ymin, ymax = min(ymin, y), max(ymax, y)
            count += 1
            neighbors = []
            if x:
                neighbors.append(index - 1)
            if x + 1 < w:
                neighbors.append(index + 1)
            if y:
                neighbors.append(index - w)
            if y + 1 < h:
                neighbors.append(index + w)
            for other in neighbors:
                if not seen[other] and classes[other] in (1, 2):
                    seen[other] = 1
                    queue.append(other)
        if count > largest:
            largest, bounds = count, [xmin, ymin, xmax, ymax]
    return bounds


def compare(a_rgb, b_rgb, w, h, background, tolerance=24):
    """Return coverage and per-channel errors; empty shared coverage has zero error.

    Disagreement regions mean coverage XOR, not colour errors. Error values are
    [R,G,B] lists over shared foreground only. Bounds use inclusive coordinates.
    """
    if w <= 0 or h <= 0 or len(a_rgb) != w * h * 3 or len(b_rgb) != len(a_rgb):
        raise ValueError('invalid dimensions or pixel count')
    if len(background) != 3 or any(not 0 <= c <= 255 for c in background):
        raise ValueError('invalid background')
    if tolerance < 0:
        raise ValueError('negative tolerance')
    classes = _classify(a_rgb, b_rgb, background, tolerance)
    only_a, only_b, both = classes.count(1), classes.count(2), classes.count(3)
    totals, maxima = [0, 0, 0], [0, 0, 0]
    for index, kind in enumerate(classes):
        if kind == 3:
            offset = index * 3
            for c in range(3):
                error = abs(a_rgb[offset + c] - b_rgb[offset + c])
                totals[c] += error
                maxima[c] = max(maxima[c], error)
    return dict(coverage_agreement=1 - (only_a + only_b) / (w * h),
                mean_abs_error=[v / both if both else 0.0 for v in totals],
                max_abs_error=maxima, pixels_only_in_a=only_a,
                pixels_only_in_b=only_b,
                largest_disagreement_bbox=_largest_region(classes, w, h))


def diff_image(a, b, background, tolerance=24):
    """Dark grey agreement, red/green exclusive coverage, grayscale shared error."""
    result = bytearray(len(a))
    for index, kind in enumerate(_classify(a, b, background, tolerance)):
        offset = index * 3
        if kind == 1:
            colour = (255, 0, 0)
        elif kind == 2:
            colour = (0, 255, 0)
        else:
            error = max(abs(a[offset + c] - b[offset + c]) for c in range(3))
            shade = 32 + (error * 223 + 127) // 255 if kind == 3 else 32
            colour = (shade,) * 3
        result[offset:offset + 3] = bytes(colour)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('a', type=Path)
    parser.add_argument('b', type=Path)
    parser.add_argument('--background', default='115,158,204')
    parser.add_argument('--tolerance', type=int, default=24)
    parser.add_argument('--quantize565', action='store_true')
    parser.add_argument('--diff', type=Path)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--min-agreement', type=float, default=0.99)
    args = parser.parse_args(argv)
    try:
        w, h, a = read_ppm(args.a)
        bw, bh, b = read_ppm(args.b)
        if (w, h) != (bw, bh):
            raise ValueError('frame dimensions differ')
        if not 0 <= args.min_agreement <= 1:
            raise ValueError('min-agreement must be between zero and one')
        background = tuple(map(int, args.background.split(',')))
        if args.quantize565:
            a, b = quantize565(a), quantize565(b)
            background = tuple(quantize565(background))
        result = compare(a, b, w, h, background, args.tolerance)
        output = json.dumps(result, indent=2) + '\n'
        print(output, end='')
        if args.json:
            args.json.write_text(output, encoding='utf-8')
        if args.diff:
            write_ppm(args.diff, w, h, diff_image(a, b, background, args.tolerance))
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    return int(result['coverage_agreement'] < args.min_agreement)


if __name__ == '__main__':
    raise SystemExit(main())
