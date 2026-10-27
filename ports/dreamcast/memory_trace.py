"""Print the per-object arena profile from verify_backend.py --dump (AI-assisted)."""
import argparse, csv, json, struct, sys
from pathlib import Path

def main():
    port = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--top', type=int, default=15)
    parser.add_argument('--csv', action='store_true',
                        help='emit all completed objects in trace order as CSV (ignores --top)')
    args = parser.parse_args()
    raw = args.dump.read_bytes()
    magic, objects, completed, failed, capacity, before, request = struct.unpack_from('<7I', raw, 0)
    if magic != 0x4d454d54: raise SystemExit('no memory trace in dump')
    after = struct.unpack_from(f'<{completed}I', raw, 28)
    names = json.loads((port / 'build/game-cgo-report.json').read_text())['compiled']
    if args.csv:
        writer = csv.writer(sys.stdout, lineterminator='\n')
        writer.writerow(['index', 'name', 'bytes_before', 'bytes_after', 'delta'])
        for i, value in enumerate(after):
            previous = after[i - 1] if i else before
            writer.writerow([i, names[i], previous, value, value - previous])
        return
    deltas = [(after[i] - (after[i - 1] if i else before), names[i]) for i in range(completed)]
    print(f'arena {capacity} bytes; before GAME.CGO {before}; after {completed}/{objects} objects {after[-1] if after else before}')
    if failed != 0xffffffff:
        print(f'object {failed} ({names[failed]}) failed; last refused request {request} bytes')
    for size, name in sorted(deltas, reverse=True)[:args.top]:
        print(f'{size:>10}  {name}')

if __name__ == '__main__':
    main()
