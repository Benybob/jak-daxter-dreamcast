"""Decode export refusals and audit actual built exports; no launch (AI-assisted), ISC."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from dcsh_tool import function, read_module
from verify_arena_jak_player import WORDS, BYTES

PORT = Path(__file__).resolve().parent
NAMES = ('start', 'kernel-dispatcher', 'read-pad', 'init-target')
MODULES = ('logic-target', 'gkernel', 'logic-target', 'logic-target')
REASONS = ('accepted', 'argument', 'entry address', 'runtime bounds/count',
           'registry record address', 'registry record fields/order', 'registry code span',
           'registry symbol type', 'registry chain tail', 'current function tag/binding', 'export owner mismatch')


def archive(path):
    raw = path.read_bytes()
    count, = struct.unpack_from('<I', raw)
    result, at = {}, 64
    for _ in range(count):
        size, = struct.unpack_from('<I', raw, at)
        name = raw[at+4:at+64].split(b'\0', 1)[0].decode()
        at += 64
        if name in result or at+size > len(raw):
            raise ValueError('invalid DGO entry '+name)
        result[name] = raw[at:at+size]
        at += (size+15) & ~15
    if at != len(raw):
        raise ValueError('DGO length')
    return result


def built_exports(port=PORT):
    """No expected byte counts or hashes copied from a different frontend."""
    packed = {kind: archive(port/'build/kernel-dgo'/(kind+'.CGO')) for kind in ('KERNEL', 'GAME')}
    rows = []
    for name, module in zip(NAMES, MODULES):
        kernel = module == 'gkernel'
        path = port/('build/kernel-dcsh' if kernel else 'build/game-dcsh')/(module+'.dcsh')
        raw = path.read_bytes()
        if packed['KERNEL' if kernel else 'GAME'][module] != raw:
            raise ValueError('packed DGO differs from built module '+module)
        code = function(read_module(path), name)
        report_path = port/'build'/('boot_gkernel.manifest.json' if kernel else 'game_logic_target.manifest.json')
        report = json.loads(report_path.read_text())
        functions = report if isinstance(report, list) else report['functions']
        matches = [f for f in functions if f['name'] == name]
        if len(matches) != 1 or matches[0]['bytes'] != len(code):
            raise ValueError('report differs from DCSH export '+name)
        rows.append(dict(name=name, module=module, bytes=len(code),
                         code_sha256=hashlib.sha256(code).hexdigest(),
                         module_sha256=hashlib.sha256(raw).hexdigest()))
    return rows


def decode(blob):
    if len(blob) == BYTES:
        words = dict(zip(WORDS, struct.unpack_from('<31I', blob)))
        return dict(format='original player v1', player=words,
                    limitation='No per-export or rejected-registry record was captured. '
                    'Error 9 is BAD_MODULE (registry structure), not a byte fingerprint mismatch.')
    if len(blob) != 520:
        raise ValueError('expected 7804-byte player v1 or 520-byte export diagnostic v1')
    version, completed, failed, prior = struct.unpack_from('<4I', blob)
    if version != 1 or completed & ~3 or failed > 2:
        raise ValueError('export diagnostic ABI')
    result = dict(format='export diagnostic v1', completed=completed, first_failed_stage=failed,
                  prior_error=prior, stages=[])
    fields = 'symbol value owner bytes error reason index at previous_entry record_entry record_bytes record_symbol next head count'.split()
    for stage in range(2):
        if not completed & (1 << stage):
            continue
        at = 16 + stage*252
        used, capacity, first = struct.unpack_from('<3I', blob, at)
        rows = []
        for i, name in enumerate(NAMES):
            row = dict(zip(fields, struct.unpack_from('<15I', blob, at+12+i*60)))
            row['name'] = name
            row['refusal'] = REASONS[row['reason']] if row['reason'] < len(REASONS) else 'unknown'
            if row['reason'] == 5:
                entry, size, record = row['record_entry'], row['record_bytes'], row['at']
                predicates = {
                    'entry below 4': entry < 4, 'entry misaligned': bool(entry & 3),
                    'code shorter than 2': size < 2, 'odd code bytes': bool(size & 1),
                    'code size overflow': size > 0xfffffffb,
                    'entry order or duplicate': entry >= row['previous_entry'],
                    'entry beyond record': entry > record,
                    'code overlaps record': entry <= record and size > record-entry,
                    'next record is not earlier': bool(row['next'] and row['next'] >= record),
                }
                row['violations'] = [key for key, bad in predicates.items() if bad]
            rows.append(row)
        result['stages'].append(dict(stage=stage+1, used=used, capacity=capacity,
                                     first_failed=first, exports=rows))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    args = parser.parse_args()
    result = decode(args.dump.read_bytes())
    result['built_exports'] = built_exports()
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
