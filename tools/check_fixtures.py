#!/usr/bin/env python3
"""Compare synthetic outputs with a committed pre-change baseline; never regenerate it."""
import hashlib
import json
from pathlib import Path
import sys

manifest, output = map(Path, sys.argv[1:])
reference = json.loads(manifest.read_text())
actual = {p.name: p for p in output.glob('*.raw')}
expected = reference['files']
errors = []
if actual.keys() != expected.keys():
    errors.append(f"file set differs: missing={expected.keys()-actual.keys()}, extra={actual.keys()-expected.keys()}")
for name in sorted(actual.keys() & expected.keys()):
    data = actual[name].read_bytes()
    if len(data) != expected[name]['size'] or hashlib.sha256(data).hexdigest() != expected[name]['sha256']:
        errors.append(f"DIFFERS: {name}; inspect {actual[name]}")
if errors:
    raise SystemExit('\n'.join(errors))
print(f"ok: {len(expected)} fixtures identical to {reference['baseline_commit'][:7]}")
