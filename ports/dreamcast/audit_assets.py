"""Measure DGO containers using the same C parser as the Dreamcast probe.

Container sizes are not linked/runtime memory requirements. This does not
identify textures, unpack compressed archives, or determine game feasibility.
"""
import argparse
import json
import math
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent


def positive_mib(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=pathlib.Path)
    probe_name = "dgo-probe.exe" if sys.platform == "win32" else "dgo-probe"
    parser.add_argument("--probe", type=pathlib.Path, default=ROOT / "build" / probe_name)
    parser.add_argument("--staging-mib", type=positive_mib, default=4.0,
                        help="hypothetical per-object staging allocation, default 4 MiB")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if not args.probe.is_file():
        parser.error("host probe missing; run make host in ports/dreamcast")
    paths = set()
    for path in args.paths:
        if path.is_dir():
            paths.update(p.resolve() for p in path.rglob("*")
                         if p.is_file() and p.suffix.lower() in (".dgo", ".cgo"))
        elif path.is_file():
            paths.add(path.resolve())
        else:
            parser.error(f"path does not exist: {path}")
    if not paths:
        parser.error("no DGO/CGO archives found")
    budget = int(args.staging_mib * 1024 * 1024)
    archives, failures = [], []
    for path in sorted(paths):
        try:
            result = subprocess.run([str(args.probe.resolve()), str(path)],
                                    capture_output=True, text=True, check=False)
        except OSError as error:
            parser.error(f"cannot run host probe: {error}")
        if result.returncode:
            failures.append({"path": str(path), "error": result.stderr.strip()})
            continue
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        summary = rows[-1]
        summary["path"] = str(path)
        summary["objects_over_staging_budget"] = [
            row for row in rows[:-1] if row["size"] > budget]
        summary["largest_objects"] = sorted(rows[:-1], key=lambda row: row["size"], reverse=True)[:10]
        archives.append(summary)
    report = {
        "upstream_commit": "6076aab5a95955443a9e1ed8f72a25bc0e2fcf9d",
        "scope": "Strict uncompressed DGO/CGO containers; payloads not decoded or linked",
        "warning": "Archive byte counts do not establish runtime memory use or a playable port",
        "staging_budget_bytes": budget,
        "stock_system_ram_bytes": 16 * 1024 * 1024,
        "archives": archives,
        "failures": failures,
    }
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end="")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
