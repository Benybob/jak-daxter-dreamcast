"""Embed one exact module file only as a diagnostic transport (AI-assisted)."""
import argparse
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('input',type=Path); parser.add_argument('output',type=Path)
args=parser.parse_args(); data=args.input.read_bytes()
args.output.write_text('/* Exact DCSH file bytes for native loader tests (AI-assisted). */\n'
                      'static const unsigned char native_fixture_file[] = {'+','.join(map(str,data))+'};\n')
