#!/usr/bin/env python3
"""Compare production-renderer fixture captures from two test_model.sh runs."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('reference', type=Path)
parser.add_argument('candidate', type=Path)
parser.add_argument('--basic', action='store_true', help='compare only features shared by the unlit basic profile')
parser.add_argument('--tolerance', type=int, default=2, help='maximum absolute RGBA channel difference')
args = parser.parse_args()
if not 0 <= args.tolerance <= 255:
    parser.error('tolerance must be 0..255')
names = ['triangle', 'animated', 'morph', 'alpha']
if not args.basic:
    names += ['light', 'skin', 'skin-morph']
for name in names:
    reference = (args.reference / (name + '.rgba')).read_bytes()
    candidate = (args.candidate / (name + '.rgba')).read_bytes()
    if len(reference) != 64 * 64 * 4 or len(candidate) != len(reference):
        raise SystemExit(f'{name}: invalid capture size')
    delta = max(abs(a - b) for a, b in zip(reference, candidate))
    print(f'{name}: max channel delta={delta}')
    if delta > args.tolerance:
        raise SystemExit(f'{name}: differs beyond tolerance {args.tolerance}')
print(f'ok: {len(names)} shared fixture frames agree')
