#!/usr/bin/env python3
"""Interleave independent CPU preparation candidates using the existing routes.

Results and screenshots stay under --out. Each game gets a fresh copy of --save;
the supplied settings are read only. A separate installed binary can be included
to detect changes caused by compilation rather than the runtime switches.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tree', type=Path, required=True)
    ap.add_argument('--data-root', required=True)
    ap.add_argument('--save', required=True)
    ap.add_argument('--ini', required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--baseline-exe', type=Path)
    ap.add_argument('--rounds', type=int, default=3)
    ap.add_argument('--duration', type=float, default=20)
    ap.add_argument('--routes', nargs='+', default=['plateau', 'castle'])
    ap.add_argument('--configs', nargs='+', default=['control', 'binding', 'writes', 'driver', 'all'])
    args = ap.parse_args()
    candidates = [('control', 0), ('binding', 1), ('writes', 2), ('driver', 4), ('all', 7)]
    unknown = set(args.configs) - {n for n, _ in candidates}
    if unknown or not args.configs or args.rounds < 1 or args.duration < 5:
        ap.error('Use known configurations, at least one round and at least five seconds per route')
    candidates = [(n, m) for n, m in candidates if n in args.configs]
    if args.baseline_exe:
        candidates.insert(0, ('installed', None))
    args.out.mkdir(parents=True, exist_ok=True)
    exe = args.tree / 'build' / ('savagewind.exe' if sys.platform == 'win32' else 'savagewind')
    manifest = {'args': {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                'candidate_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'ini_sha256': hashlib.sha256(Path(args.ini).read_bytes()).hexdigest()}
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    for round_index in range(args.rounds):
        offset = round_index % len(candidates)
        order = candidates[offset:] + candidates[:offset]
        if round_index % 2:
            order.reverse()
        for label, mask in order:
            folder = args.out / ('r{}-{}'.format(round_index + 1, label))
            argv = [sys.executable, str(args.tree / 'tools' / 'perf_routes.py'), 'run',
                    '--tree', str(args.tree), '--data-root', args.data_root,
                    '--save', args.save, '--ini', args.ini, '--out', str(folder),
                    '--config', label, '--exe', str(args.baseline_exe if mask is None else exe),
                    '--size', '2560x1440', '--runs', '1', '--first-run', str(round_index + 1),
                    '--duration', str(args.duration), '--settle', '8', '--after-boot', '3',
                    '--sample', '0', '--routes', *args.routes, '--env',
                    'SAVAGEWIND_VK_CPU_OPTS={}'.format(mask or 0), 'SAVAGEWIND_VK_PROF=2']
            print('ROUND {} {} mask {}'.format(round_index + 1, label, mask), flush=True)
            subprocess.run(argv, check=True)
            rows = list(csv.DictReader((folder / 'results.csv').open()))
            if len(rows) != len(args.routes) or any(float(row['others']) for row in rows):
                raise RuntimeError('Missing routes or another game contaminated ' + str(folder))
    print('DONE: ' + str(args.out), flush=True)


if __name__ == '__main__':
    main()
