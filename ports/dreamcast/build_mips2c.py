"""Build the PC port's jak1 mips2c functions natively for SH-4 (AI-assisted), ISC.

The C++ translations in game/mips2c/jak1_functions compile for SH-4 unchanged,
against mips2c_dc.cpp's services and mips2c_dc/immintrin.h (a scalar stand-in for
the SSE intrinsics vu.h uses). This script:

- finds every gLinkedFunctionTable.reg(...) in the selected files, with the
  namespace whose execute/link() it belongs to;
- gives each registered name its GOAL signature: a (def-mips2c NAME TYPE) in
  goal_src, a method's entry in all-types.gc (by its ";; N" id), or a
  define-extern there. Helpers typed only `function` are called by other mips2c
  code through jalr with integer registers, so all eight arguments are u64;
- writes build/mips2c/mips2c_dc_table.inc: one C wrapper per name with that
  native signature (float where GOAL has float, u64 otherwise), the link()
  list, and the name -> wrapper table;
- compiles the files and mips2c_dc.cpp with sh-elf-g++ into
  build/mips2c/libdcmips2c.a;
- builds the host oracle (host_mips2c_oracle.cpp with the unchanged files and real
  SSE, as desktop runs them) and writes its results for mips2c_cases.h to
  build/mips2c/mips2c_cases_expect.h, which the Flycast boot compares against.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

DEFAULT_FILES = ('collide_cache', 'sky_tng', 'collide_edge_grab', 'collide_mesh', 'generic_effect',
                 'generic_merc', 'ripple', 'bones', 'generic_effect2', 'collide_func', 'collide_probe', 'generic_tie',
                 'joint', 'merc_blend_shape', 'ocean', 'ocean_vu0', 'shadow', 'sparticle', 'sparticle_launcher', 'tfrag',
                 'tie_methods', 'draw_string', 'texture', 'time_of_day')
CPP_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)


def cpp_code(text):
    return CPP_TOKEN.sub(lambda m: m[0] if m[0].startswith('"') else ' ', text)


def registrations(path):
    """(namespace, name) for each reg() call, in file order."""
    code = cpp_code(path.read_text(encoding='utf-8'))
    events = [(m.start(), 'ns', m[1]) for m in re.finditer(r'\bnamespace\s+(\w+)\s*\{', code)]
    events += [(m.start(), 'reg', m[1]) for m in
               re.finditer(r'\bgLinkedFunctionTable\s*\.\s*reg\s*\(\s*"((?:\\.|[^"\\])*)"', code)]
    current, out = None, []
    for _, kind, value in sorted(events):
        if kind == 'ns':
            current = value
        else:
            if not current:
                raise ValueError(f'{path.name}: registration {value} outside a function namespace')
            out.append((current, value))
    return out


def split_type(text):
    """Top-level items of a parenthesized GOAL type list."""
    text = text.strip()
    if not text.startswith('('):
        return text
    items, depth, token = [], 0, ''
    for ch in text[1:-1]:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch.isspace() and depth == 0:
            if token:
                items.append(token)
            token = ''
        else:
            token += ch
    if token:
        items.append(token)
    return items


def balanced(text, start):
    depth = 0
    for i in range(start, len(text)):
        depth += {'(': 1, ')': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
    raise ValueError('unbalanced type')


# Methods GOAL calls through a cast to another function type: their declared type
# leaves out arguments the code reads. fill-from-background calls these two as
# its import-mesh-func, (function collide-cache object none), with the mesh in a1.
CALLED_AS = {
    '(method 27 collide-cache)': (['u64', 'u64'], 'u64'),
    '(method 28 collide-cache)': (['u64', 'u64'], 'u64'),
}


def signature(name, goal_text, all_types):
    """(argument kinds, result kind); kinds are 'float' or 'u64'."""
    if name in CALLED_AS:
        return CALLED_AS[name]
    def kinds(types):
        out = []
        for t in types:
            if t in ('uint128', 'int128', 'vector4w', 'vf'):
                raise ValueError(f'{name}: 128-bit argument {t} unsupported')
            out.append('float' if t == 'float' else 'u64')
        return out
    m = re.search(r'\(def-mips2c\s+' + re.escape(name) + r'\s+', goal_text)
    if m:
        type_text = balanced(goal_text, m.end()) if goal_text[m.end()] == '(' else goal_text[m.end():].split(')')[0]
    else:
        method = re.fullmatch(r'\(method (\d+) ([^\s()]+)\)', name)
        type_text = None
        if method:
            index, owner = method.groups()
            line, seen = None, owner
            while seen and not line:  # an inherited method is declared on an ancestor
                block = re.search(r'\(deftype ' + re.escape(seen) + r' \((\S+)\).*?(?=\n\(deftype |\Z)', all_types, re.S)
                if not block:
                    break
                line = re.search(r'^\s*\((\S+) (\(.*\)) (\S+)\)\s*;; ' + index + r'\b', block[0], re.M)
                seen = block[1] if block[1] != seen else None
            if not line:
                raise ValueError(f'{name}: no method {index} in deftype {owner} or its parents')
            return kinds(split_type(line[2])), ('float' if line[3] == 'float' else 'u64')
        m = re.search(r'\(define-extern ' + re.escape(name) + r' ', all_types)
        if m:
            type_text = balanced(all_types, m.end()) if all_types[m.end()] == '(' else all_types[m.end():].split(')')[0]
    if not type_text or type_text.strip() == 'function':
        return ['u64'] * 8, 'u64'  # internal helper: called through jalr with GPRs
    items = split_type(type_text)
    if not isinstance(items, list) or items[0] != 'function':
        raise ValueError(f'{name}: not a function type: {type_text}')
    return kinds(items[1:-1]), ('float' if items[-1] == 'float' else 'u64')


def table(entries):
    lines = ['/* Generated by build_mips2c.py (AI-assisted). */', 'namespace Mips2C::jak1 {']
    for namespace in dict.fromkeys(ns for ns, _, _ in entries):
        lines.append(f'namespace {namespace} {{ u64 execute(void*); void link(); }}')
    lines.append('}')
    lines.append('static void (*const dc_links[])() = {')
    lines += [f'  &Mips2C::jak1::{ns}::link,' for ns in dict.fromkeys(ns for ns, _, _ in entries)]
    lines.append('};')
    lines.append('namespace {')
    for i, (_, name, (args, result)) in enumerate(entries):
        if len(args) > 8:
            raise ValueError(f'{name}: more than eight arguments')
        params = ', '.join(f'{"float" if k == "float" else "u64"} a{j}' for j, k in enumerate(args)) or 'void'
        values = [f'to_bits(a{j})' if k == 'float' else f'a{j}' for j, k in enumerate(args)]
        values += ['0'] * (8 - len(values))
        call = f'dc_run({i}, args)'
        ret = f'return from_bits({call});' if result == 'float' else f'return {call};'
        rtype = 'float' if result == 'float' else 'u64'
        lines.append(f'/* {name} */')
        lines.append(f'{rtype} dc_w{i}({params}) {{ const u64 args[8] = {{{", ".join(values)}}}; {ret} }}')
        # A generic entry for the differential cases: eight u64 (float arguments as
        # bits) through the typed wrapper, so the cases exercise the native ABI.
        actuals = ', '.join(f'from_bits(a[{j}])' if k == 'float' else f'a[{j}]' for j, k in enumerate(args))
        call_typed = f'dc_w{i}({actuals})'
        lines.append(f'u64 dc_g{i}(const u64* a) {{ return {"to_bits(" + call_typed + ")" if result == "float" else call_typed}; }}')
    lines.append('struct DcEntry { const char* name; uint32_t entry; u64 (*generic)(const u64*); };')
    lines.append('const DcEntry dc_entries[] = {')
    for i, (_, name, _) in enumerate(entries):
        lines.append(f'  {{"{name}", (uint32_t)(uintptr_t)&dc_w{i}, &dc_g{i}}},')
    lines.append('};')
    lines.append('}  // namespace')
    return '\n'.join(lines) + '\n'


def main():
    port = Path(__file__).resolve().parent
    root = port.parents[1]
    work = port.parents[2]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--files', nargs='+', default=list(DEFAULT_FILES))
    parser.add_argument('--gxx', type=Path, default=work / 'dc-sdk/sh-elf/bin/sh-elf-g++.exe')
    parser.add_argument('--ar', type=Path, default=work / 'dc-sdk/sh-elf/bin/sh-elf-ar.exe')
    parser.add_argument('--host-gxx', type=Path, default=Path('C:/msys64/mingw64/bin/g++.exe'))
    parser.add_argument('--table-only', action='store_true', help='write the generated table and stop')
    args = parser.parse_args()
    goal_text = '\n'.join(p.read_text(encoding='utf-8', errors='replace')
                          for p in sorted((root / 'goal_src/jak1').rglob('*.gc')))
    all_types = (root / 'decompiler/config/jak1/all-types.gc').read_text(encoding='utf-8', errors='replace')
    sources = [root / 'game/mips2c/jak1_functions' / f'{name}.cpp' for name in args.files]
    entries = []
    for source in sources:
        for namespace, name in registrations(source):
            entries.append((namespace, name, signature(name, goal_text, all_types)))
    out = port / 'build/mips2c'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'mips2c_dc_table.inc').write_bytes(table(entries).encode())
    for _, name, (a, r) in entries:
        print(f'{name}: ({" ".join(a)}) -> {r}')
    if args.table_only:
        return
    # -ffp-contract=off: GCC would fuse a*b+c into SH-4 FMAC (one rounding); desktop
    # x86-64 rounds the product and the sum separately, as the translated VU code does.
    # -Os: about 30% less code than -O2 (837 KB of text for 21 files at -O2), which the
    # KOS heap needs while GAME.CGO loads; the differential cases still match bit for bit.
    flags = ['-std=gnu++20', '-Os', '-m4-single', '-ml', '-ffp-contract=off', '-ffunction-sections', '-fdata-sections',
             '-fno-exceptions', '-fno-rtti', '-D__forceinline=__attribute__((always_inline))', '-DFMT_HEADER_ONLY',
             f'-I{port / "mips2c_dc"}', f'-I{port}', f'-I{root}', f'-I{root / "third-party"}',
             f'-I{root / "third-party/fmt/include"}']
    objects = []
    for source in sources + [port / 'mips2c_dc.cpp']:
        obj = out / (source.stem + '.o')
        result = subprocess.run([str(args.gxx), *flags, '-c', str(source), '-o', str(obj)],
                                capture_output=True, text=True)
        if result.returncode:
            sys.exit(f'{source.name} failed:\n' + '\n'.join(result.stderr.splitlines()[:20]))
        objects.append(obj)
    library = out / 'libdcmips2c.a'
    library.unlink(missing_ok=True)
    subprocess.run([str(args.ar), 'rcs', str(library), *map(str, objects)], check=True)
    print(f'{len(entries)} functions from {len(sources)} files -> {library.relative_to(port)}')
    # The desktop-equivalent oracle for the differential cases (mips2c_cases.h).
    oracle = out / 'mips2c-oracle.exe'
    env = dict(os.environ, PATH=str(args.host_gxx.parent) + os.pathsep + os.environ.get('PATH', ''))
    result = subprocess.run([str(args.host_gxx), '-std=gnu++20', '-O2', '-msse4.1',
                             '-include', str(port / 'mips2c_dc/host_prefix.h'), f'-I{port}', f'-I{root}',
                             f'-I{root / "third-party"}', f'-I{root / "third-party/fmt/include"}', '-DFMT_HEADER_ONLY',
                             str(port / 'host_mips2c_oracle.cpp'), *map(str, sources), '-o', str(oracle)],
                            capture_output=True, text=True, env=env)
    if result.returncode:
        sys.exit('host oracle failed:\n' + '\n'.join(result.stderr.splitlines()[:20]))
    run = subprocess.run([str(oracle)], capture_output=True, text=True, env=env)
    if run.returncode:
        sys.exit(f'host oracle run failed ({run.returncode}): {run.stderr.strip()}')
    (out / 'mips2c_cases_expect.h').write_bytes(
        ('/* Generated by build_mips2c.py from the host oracle (AI-assisted). */\n' + run.stdout).encode())
    print(run.stdout.strip())


if __name__ == '__main__':
    main()
