"""Group batch frontend export refusals by exact error text (AI-assisted)."""
import argparse
import json
from pathlib import Path


def group_refusals(status, by_file=False):
    """Count export_errors records, excluding successful exports and fatal failed status."""
    grouped = {}
    for refusal in status.get('export_errors', []):
        error, source = refusal['error'], refusal['source']
        if not isinstance(error, str) or not isinstance(source, str):
            raise ValueError('export_errors entries require string source and error')
        key, value = (source, error) if by_file else (error, source)
        grouped.setdefault(key, []).append(value)
    key_field, values_field = ('source', 'errors') if by_file else ('error', 'files')
    return [{key_field: key, 'count': len(values), values_field: sorted(values)}
            for key, values in sorted(grouped.items(), key=lambda item: (-len(item[1]), item[0]))]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('status', nargs='?', type=Path,
                        default=Path(__file__).resolve().parent / 'build/game-batch-status.json')
    parser.add_argument('--json', action='store_true', help='emit machine-readable groups')
    parser.add_argument('--by-file', action='store_true', help='group errors under each source file')
    args = parser.parse_args(argv)
    try:
        groups = group_refusals(json.loads(args.status.read_text(encoding='utf-8')), args.by_file)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        parser.error(str(error))
    if args.json:
        print(json.dumps(dict(total=sum(g['count'] for g in groups), groups=groups), indent=2))
    elif not groups:
        print('No export refusals.')
    else:
        for group in groups:
            if args.by_file:
                print(f"{group['source']} ({group['count']} refusal(s))")
                for error in group['errors']:
                    print('  ' + error.replace('\n', '\n  '))
                continue
            print(f"{group['count']} export refusal(s): {group['error']}")
            for source in group['files']:
                print(f'  {source}')


if __name__ == '__main__':
    main()
