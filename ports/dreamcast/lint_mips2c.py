"""Linear mips2c lint (AI-assisted), ISC license; see LICENSE.

Scan straight-line source order, without control-flow analysis. Findings are
candidates for a human to confirm, not proof of a runtime bug. Helper vis writes
are collected transitively regardless of call order or conditional execution.
Report the first premature read of each register in each function.
"""
import argparse
import json
from pathlib import Path
import re

REG = r'\b(?:at|t[0-9]|s[0-7]|v[01]|gp|fp|ra|sp|r0|a[0-3])\b'
IGNORED = {'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3',
           'sp', 's6', 's7', 'r0'}
CALLEE = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'gp', 'fp', 'ra'}
STORES = {'sb', 'sh', 'sw', 'sd', 'sq', 'swc1', 'sqc2', 'store_symbol'}
READ_ONLY = STORES | {'gpr_src', 'gpr_addr', 'sgpr64', 'jalr',
                      'mtc1', 'mov128_vf_gpr'}
# These methods have a non-GPR destination (their remaining GPRs are reads).
NON_GPR = {'lwc1', 'lqc2', 'lw_float_constant'}
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)

def clean(text):
    """Blank comments and literals while preserving offsets and line numbers."""
    return TOKEN.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text)

def functions(code):
    """Find execute/helpers with a context parameter, retaining namespace scope."""
    scopes = []; result = []; cursor = 0
    token = re.compile(r'namespace\s+([\w:]+)\s*\{|\b(?:u64|void|bool|int|u16|u32|u128)\s+(\w+)\s*\([^;{}]*\b(?:ExecutionContext\s*\*|void\s*\*\s*ctxt)[^;{}]*\)\s*\{|[{}]')
    for match in token.finditer(code):
        if match.start() < cursor:
            continue
        if match[1]:
            scopes.append(match[1])
        elif match[2]:
            start = match.end(); end = start; depth = 1
            while end < len(code) and depth:
                depth += (code[end] == '{') - (code[end] == '}'); end += 1
            name = '::'.join([s for s in scopes if s] + [match[2]])
            result.append((name, start, code[start:end-1]))
            cursor = end
        elif match[0] == '{':
            scopes.append(None)
        elif scopes:
            scopes.pop()
    return result

def vis_events(body):
    """Yield reads before writes within each semicolon-delimited statement."""
    for statement in re.finditer(r'[^;]+(?:;|$)', body):
        reads = []; writes = []
        for match in re.finditer(r'\bvis\s*\[\s*(?:vi0*(\d+)|(\d+))\s*\]', statement[0]):
            before = statement[0][:match.start()]
            if re.search(r'\bu16\s*$', before):
                continue  # array declaration
            index = int(match[1] or match[2])
            tail = statement[0][match.end():]
            assignment = re.match(r'\s*(=(?!=)|[+\-*/&|^]=|\+\+|--)', tail)
            position = statement.start()+match.start()
            if assignment:
                writes.append((position, index, True))
                if assignment[1] != '=': reads.append((position, index, False))
            else:
                reads.append((position, index, False))
        yield from reads
        yield from writes

def lint_file(path, root):
    code = clean(path.read_text())
    funcs = functions(code)
    by_name = {name: body for name, _, body in funcs}
    writes = {name: {i for _, i, write in vis_events(body) if write}
              for name, _, body in funcs}
    calls = {}
    for name, _, body in funcs:
        scope = name.rsplit('::', 1)[0] if '::' in name else ''
        calls[name] = {f'{scope}::{m[1]}' if scope else m[1]
                       for m in re.finditer(r'\b(\w+)\s*\(', body)} & by_name.keys()
    # Helpers can delegate to other helpers.
    while True:
        merged = {n: writes[n] | set().union(*(writes[c] for c in calls[n])) for n in writes}
        if merged == writes: break
        writes = merged
    findings = []
    try: filename = path.relative_to(root).as_posix()
    except ValueError: filename = path.as_posix()
    for name, start, body in funcs:
        def report(kind, item, position):
            findings.append(dict(file=filename, function=name, kind=kind,
                                 register_or_index=item, line=code.count('\n', 0, start+position)+1))
        initialized = set(IGNORED)
        reported = set()
        # Statement order lets assignments read their RHS before writing the LHS.
        for statement in re.finditer(r'[^;]+(?:;|$)', body):
            text = statement[0]; destinations = []; excluded = []
            for call in re.finditer(r'(?:c->(\w+)|\b(get_fake_spad_addr))\s*\(\s*([^,()]*)', text):
                op = call[1] or call[2]; first = call[3].strip()
                if first not in re.findall(REG, first): continue
                if op in {'sd', 'sq'}:
                    save = re.match(r'\s*,\s*[^,()]+,\s*sp\s*\)', text[call.end():])
                    if save and first in CALLEE:
                        excluded.append((call.start(), call.end()+save.end())); continue
                if op not in READ_ONLY | NON_GPR:
                    destinations.append((call.start(3), call.end(3), first))
            for access in re.finditer(r'c->gprs\s*\[\s*('+REG+r')\s*\][^;=]*=(?!=)', text):
                destinations.append((access.start(1), access.end(1), access[1]))
            for register in re.finditer(REG, text):
                if any(a <= register.start() < b for a, b in excluded): continue
                if any(a <= register.start() < b for a, b, _ in destinations): continue
                if register[0] not in initialized and register[0] not in reported:
                    reported.add(register[0])
                    report('register_read_before_write', register[0], statement.start()+register.start())
            initialized.update(r for _, _, r in destinations)
        pending = set()
        provided = set().union(*(writes[c] for c in calls[name]))
        for position, index, write in vis_events(body):
            if name.rsplit('::', 1)[-1] == 'execute':
                if not write and index not in provided:
                    report('vis_read_without_helper_write', index, position)
            elif write:
                if index in pending: report('vis_overwritten_without_read', index, position)
                pending.add(index)
            else:
                pending.discard(index)
    return findings

def lint(root, files=None):
    root = Path(root).resolve()
    paths = ([Path(p) if Path(p).is_absolute() else root/p for p in files]
             if files else sorted((root/'game/mips2c/jak1_functions').glob('*.cpp')))
    findings = [row for path in paths for row in lint_file(path, root)]
    return sorted(findings, key=lambda r: (r['file'], r['function'], r['kind'], str(r['register_or_index']), r['line']))

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('files', nargs='*')
    args = parser.parse_args(argv)
    print(json.dumps(lint(args.root, args.files), indent=2))

if __name__ == '__main__':
    main()
