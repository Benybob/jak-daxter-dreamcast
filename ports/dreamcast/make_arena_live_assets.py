"""Bind live-camera coverage to the staged beach assets (AI-assisted), ISC.

Reads only the existing fixture. Does not regenerate or change its files.
The instance splitter writes ALL occupied cells, so an unlisted coordinate is
known empty, conditional on using that unmodified complete split as input.
"""
import argparse
import json
from pathlib import Path
import re
import zlib


def header(root):
    root = Path(root)
    report = json.loads((root / 'FLY.json').read_text())
    crop = report['crop']
    baseline = report['frames'] == 16 and report['length_m'] == 6
    recorded = (report['frames'] == 181 and report.get('prediction_only') is True
                and report.get('travel_cap_xz') == 6 and report.get('travel_cap_y') == 1
                and re.fullmatch('[0-9a-f]{64}', report.get('recording_sha256', '')))
    if (not (baseline or recorded) or len(crop) != 4
            or any(not isinstance(x, (int, float)) or not -1e6 < x < 1e6 for x in crop)
            or not crop[0] < crop[1] or not crop[2] < crop[3]):
        raise ValueError('requires the baseline or bounded recorded live beach crop')
    if (root / 'package.dci').read_bytes()[:4] not in (b'DCI2', b'DCI3'):
        raise ValueError('live target requires a baseline DCI2/DCI3 cell split')
    rows = []
    for path in sorted((root / 'cells').glob('*.dci')):
        match = re.fullmatch(r'(-?\d+)_(-?\d+)\.dci', path.name)
        blob = path.read_bytes()
        if not match or not blob or len(blob) % 52:
            raise ValueError(f'bad baseline cell {path.name}')
        ix, iz = map(int, match.groups())
        if path.name != f'{ix}_{iz}.dci':
            raise ValueError('noncanonical cell name')
        if abs(ix) > 62500 or abs(iz) > 62500:
            raise ValueError('cell outside renderer bounds')
        rows.append((ix, iz, len(blob) // 52, zlib.crc32(blob)))
    if not rows or len(rows) > 4096:
        raise ValueError('occupied cell ledger is empty or too large')
    text = ['/* Generated live asset ledger (AI-assisted), ISC. */',
            'static const double live_crop[4]={' + ','.join(repr(float(v)) for v in crop) + '};',
            'static const dc_live_cell_entry live_cell_ledger[]={']
    text += [f'{{{x},{z},{n}u,{crc}u}},' for x, z, n, crc in rows]
    text.append('};')
    return '\n'.join(text) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = header(args.assets)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result, newline='\n')
