"""Print the per-object arena profile from verify_backend.py --dump (AI-assisted)."""
import argparse, csv, json, struct, sys
from pathlib import Path

def read_trace(path, names=None):
    raw = Path(path).read_bytes()
    header = struct.unpack_from('<7I', raw, 0)
    magic, objects, completed, failed, capacity, before, request = header
    if magic != 0x4d454d54:
        raise ValueError('no memory trace in dump')
    if completed > objects or (names is not None and completed > len(names)):
        raise ValueError('completed object count exceeds trace or report')
    after = struct.unpack_from(f'<{completed}I', raw, 28)
    rows = [(i, names[i] if names is not None else None,
             after[i] - (after[i - 1] if i else before)) for i in range(completed)]
    return header, after, rows

def main():
    port = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--top', type=int, default=15)
    parser.add_argument('--csv', action='store_true',
                        help='emit all completed objects in trace order as CSV (ignores --top)')
    parser.add_argument('--compare', type=Path, metavar='OTHER_DUMP',
                        help='compare all object deltas: change = OTHER_DUMP minus dump (ignores --top)')
    args = parser.parse_args()
    try:
        header, after, rows = read_trace(args.dump)
        magic, objects, completed, failed, capacity, before, request = header
        names = json.loads((port / 'build/game-cgo-report.json').read_text())['compiled']
        if completed > len(names):
            raise ValueError('completed object count exceeds report')
        rows = [(i, names[i], size) for i, _, size in rows]
        other_rows = read_trace(args.compare, names)[2] if args.compare else None
        if other_rows is not None and [r[:2] for r in rows] != [r[:2] for r in other_rows]:
            raise ValueError('comparison requires the same completed object names')
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.error(str(error))
    if other_rows is not None:
        comparisons = [(i, name, size, other[2], other[2] - size)
                       for (i, name, size), other in zip(rows, other_rows)]
        if args.csv:
            writer = csv.writer(sys.stdout, lineterminator='\n')
            writer.writerow(['index', 'name', 'delta_before', 'delta_after', 'change'])
            writer.writerows(comparisons)
        else:
            print('change = OTHER_DUMP delta - DUMP delta')
            for i, name, old, new, change in comparisons:
                print(f'{i}\t{name}\t{old}\t{new}\t{change:+d}')
        return
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
