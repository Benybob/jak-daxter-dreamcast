"""Run a Dreamcast ELF under Flycast and capture what it plays (AI-assisted), ISC.

    python capture_audio.py FLYCAST ELF --nm NM --output OUT [--suite S --tests N]
                            [--expect-period SAMPLES] [--expect-peak LEVEL]

verify_backend.py runs Flycast with the null audio backend, which discards sound.
This runs the same verifier with Flycast's SDL backend and SDL's disk driver, so
every frame Flycast plays is written to OUT/sdlaudio.raw (signed 16-bit stereo
at 44.1 kHz), and also saved as OUT/capture.wav. It then reports each burst of
sound: length, median half-period in samples and peak. The disk driver runs
faster than real time, so gaps between bursts are not real timing.

--expect-period and --expect-peak turn the report into a check: the exit status
is 1 unless some burst has that median half-period (within one sample) and a
peak within 15% of the level. The verifier's own result still has to pass.
"""
import argparse
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import wave

PORT = Path(__file__).resolve().parent
RATE = 44100


def bursts(left, threshold=800, gap=4000):
    """(start, end) runs of samples above threshold, split by quiet gaps."""
    loud = [i for i, x in enumerate(left) if abs(x) > threshold]
    if not loud:
        return []
    out, start, prev = [], loud[0], loud[0]
    for i in loud[1:]:
        if i - prev > gap:
            out.append((start, prev))
            start = i
        prev = i
    out.append((start, prev))
    return out


def describe(left):
    rows = []
    for a, z in bursts(left):
        seg = left[a:z + 1]
        crossings = [i for i in range(1, len(seg)) if (seg[i - 1] < 0) != (seg[i] < 0)]
        gaps = [crossings[i] - crossings[i - 1] for i in range(1, len(crossings))]
        rows.append(dict(samples=len(seg), seconds=round(len(seg) / RATE, 4),
                         half_period=statistics.median(gaps) if gaps else None,
                         peak=max(abs(x) for x in seg)))
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('flycast', type=Path)
    ap.add_argument('elf', type=Path)
    ap.add_argument('--nm', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--suite', default='codegen')
    ap.add_argument('--tests', type=int, default=17)
    ap.add_argument('--timeout', type=float, default=60)
    ap.add_argument('--expect-period', type=float)
    ap.add_argument('--expect-peak', type=float)
    a = ap.parse_args(argv)
    a.output.mkdir(parents=True, exist_ok=True)
    verifier = a.output / 'verify_audio.py'
    source = (PORT / 'verify_backend.py').read_text()
    if source.count("'audio:backend=null'") != 1:
        sys.exit('verify_backend.py no longer selects the null audio backend in one place')
    verifier.write_text(source.replace("'audio:backend=null'", "'audio:backend=sdl2'"))
    env = dict(os.environ, SDL_AUDIODRIVER='disk', SDL_AUDIO_DRIVER='disk', SDL_DISKAUDIODELAY='0',
               PYTHONPATH=os.pathsep.join(filter(None, [str(PORT), os.environ.get('PYTHONPATH')])))
    run = a.output / 'run'
    result = subprocess.run([sys.executable, str(verifier), str(a.flycast), str(a.elf), '--nm', str(a.nm),
                             '--output', str(run), '--suite', a.suite, '--tests', str(a.tests),
                             '--timeout', str(a.timeout)], env=env, cwd=PORT, capture_output=True, text=True)
    raw = run / 'sdlaudio.raw'
    if not raw.exists():
        sys.exit(f'no audio captured (verifier exit {result.returncode})')
    data = raw.read_bytes()
    frames = len(data) // 4
    left = list(struct.unpack('<%dh' % (frames * 2), data[:frames * 4])[0::2])
    with wave.open(str(a.output / 'capture.wav'), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data[:frames * 4])
    rows = describe(left)
    report = dict(verifier_exit=result.returncode, frames=frames, bursts=rows)
    ok = result.returncode == 0
    if a.expect_period is not None or a.expect_peak is not None:
        match = [r for r in rows
                 if (a.expect_period is None or (r['half_period'] is not None
                                                  and abs(r['half_period'] - a.expect_period) <= 1))
                 and (a.expect_peak is None or abs(r['peak'] - a.expect_peak) <= 0.15 * a.expect_peak)]
        report['expected_burst_found'] = bool(match)
        ok = ok and bool(match)
    (a.output / 'capture.json').write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
