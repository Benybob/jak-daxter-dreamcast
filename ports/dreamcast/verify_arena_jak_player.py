"""Audit original-player receipts without launching anything (AI-assisted), ISC."""
import argparse
import json
import math
from pathlib import Path
import struct

WORDS = ('version active phase failure frames pair_available pair_shortfall sidekick_loaded '
         'target thread init_entry read_entry read_shim read_calls read_returns attempts resumes '
         'pad_services pad_valid control cpad state_changes moving_reads neutral_reads moving_changes '
         'neutral_stable restored error site sidekick_available sidekick_shortfall').split()
BYTES = 7804


def need(ok, text):
    if not ok:
        raise ValueError(text)


def audit(blob, host_fixture=False):
    need(len(blob) == BYTES, 'player evidence ABI/length')
    w = dict(zip(WORDS, struct.unpack_from('<31I', blob)))
    for key, expected in dict(version=1, active=0, phase=3, failure=0, frames=160,
                              restored=1, error=0, site=0).items():
        need(w[key] == expected, f'{key}={w[key]}, expected {expected}')
    need(w['sidekick_loaded'] in (0, 1), 'sidekick admission flag')
    need(w['pair_shortfall'] == max(0, 1979744-w['pair_available']), 'exact pair deficit')
    if w['sidekick_loaded']:
        need(w['pair_available'] >= 1979744+65536, 'pair admitted without headroom')
    need(w['sidekick_shortfall'] == (0 if w['sidekick_loaded'] else max(0,661456-w['sidekick_available'])),
         'exact remaining sidekick deficit')
    need(w['read_entry'] != w['read_shim'] and w['read_entry'] != w['init_entry'], 'original entries/shim alias')
    for key in ('target', 'thread', 'init_entry', 'read_entry', 'read_shim', 'control', 'cpad'):
        need(w[key] != 0, 'missing '+key)
        if not host_fixture:
            need(0x0c010000 <= (w[key] & 0x1fffffff) < 0x0d000000, key+' outside stock main RAM')
    need(w['read_calls'] == w['read_returns'] and w['read_returns'] >= 64, 'original read-pad calls/returns')
    need(w['attempts'] >= w['resumes'] >= 160 and w['pad_services'] == w['pad_valid'] >= 160,
         'original target resume/pad services')
    moving = stable = 0
    previous = None
    for i in range(160):
        seq, state, status, resumes, reads, pads, joyx, buttons, x, y, z, magnitude = struct.unpack_from('<6IiI4f', blob, 124+i*48)
        expected = 96 if 32 <= i < 64 else -96 if 96 <= i < 128 else 0
        need(seq == i+1 and joyx == expected and buttons == 0, 'recorded hardware sequence')
        need(state and status and all(math.isfinite(v) for v in (x,y,z,magnitude)) and 0 <= magnitude <= 1.001,
             'invalid state/control output')
        need(resumes and pads and reads <= w['read_returns'], 'invalid frame counters')
        if previous:
            need(resumes > previous[0] and pads > previous[2] and reads >= previous[1], 'stalled target/pad recording')
            d2 = (x-previous[3])**2 + (z-previous[4])**2
            moving += bool(joyx and d2 > 16)
            stable += bool(i >= 144 and d2 <= 16)
        previous = (resumes,reads,pads,x,z)
    need(previous[:3] == (w['resumes'],w['read_returns'],w['pad_valid']), 'final trace counters')
    need(moving == w['moving_changes'] >= 16 and stable == w['neutral_stable'] >= 12, 'position response/neutral stability')
    need(w['moving_reads'] >= 32 and w['neutral_reads'] >= 32, 'original read-pad neutral/moving response')
    return {k:v for k,v in w.items() if k not in ('active',)}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dump', type=Path)
    a = p.parse_args()
    print(json.dumps(audit(a.dump.read_bytes()), indent=2))
