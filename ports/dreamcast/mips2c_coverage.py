"""Line coverage of the mips2c differential cases (AI-assisted), ISC.

Builds the host oracle (host_mips2c_oracle.cpp with the unchanged translated
files, as build_mips2c.py does) with gcov instrumentation, runs every case in
mips2c_cases.h, and prints each translated function's share of lines run,
lowest first. A function at 0% is one no case reaches; the gaps that are left on
purpose are listed in MIPS2C.md. It found collide-do-primitives' dropped
arguments: the function had never run.

  python mips2c_coverage.py [--msys C:/msys64] [--below 90]

It needs build/mips2c/mips2c_dc_table.inc, which build_mips2c.py writes
(--table-only is enough).
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

PORT = Path(__file__).resolve().parent
sys.path.insert(0, str(PORT))
import build_mips2c  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--msys', type=Path, default=Path('C:/msys64'))
    parser.add_argument('--below', type=float, default=101.0, help='only list functions under this percentage')
    args = parser.parse_args()
    bin_dir = args.msys / 'mingw64/bin'
    env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'])
    out = PORT / 'build/mips2c/coverage'
    out.mkdir(parents=True, exist_ok=True)
    for stale in [*out.glob('*.gcda'), *out.glob('*.gcno'), *out.glob('*.gcov')]:
        stale.unlink()  # counts from an earlier run would add up
    root = PORT.parents[1]
    sources = [root / 'game/mips2c/jak1_functions' / f'{f}.cpp' for f in build_mips2c.DEFAULT_FILES]
    flags = ['-std=gnu++20', '-O0', '--coverage', '-msse4.1', '-include', 'mips2c_dc/host_prefix.h', '-I.', '-I../..',
             '-I../../third-party', '-I../../third-party/fmt/include', '-DFMT_HEADER_ONLY']
    objects = []
    for source in sources + [PORT / 'host_mips2c_oracle.cpp']:
        obj = out / (source.name + '.o')
        subprocess.run([str(bin_dir / 'g++.exe'), *flags, '-c', str(source), '-o', str(obj)], cwd=PORT, env=env,
                       check=True)
        objects.append(str(obj))
    exe = out / 'oracle_coverage.exe'
    subprocess.run([str(bin_dir / 'g++.exe'), '--coverage', *objects, '-o', str(exe)], env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rows = []
    for source in sources:
        report = subprocess.run([str(bin_dir / 'gcov.exe'), '-f', '-n', '-o', str(out / (source.name + '.o')),
                                 str(source)], cwd=out, env=env, capture_output=True, text=True, check=True).stdout
        for mangled, pct, lines in re.findall(r"Function '([^']+)'\nLines executed:([\d.]+)% of (\d+)", report):
            m = re.fullmatch(r'_ZN6Mips2C4jak1(\d+)(\w+)7executeEPv', mangled)
            if m:
                rows.append((float(pct), int(lines), source.stem, m[2][:int(m[1])]))
    for pct, lines, stem, name in sorted(rows):
        if pct < args.below:
            print(f'{pct:6.1f}% of {lines:4d}  {stem}: {name}')


if __name__ == '__main__':
    main()
