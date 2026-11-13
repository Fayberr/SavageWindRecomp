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
from types import SimpleNamespace


def run_order(candidates, index):
    offset = (index // 2) % len(candidates)
    order = candidates[offset:] + candidates[:offset]
    return list(reversed(order)) if index % 2 else order


def live(args, candidates, exe):
    import perf_routes as routes
    a = SimpleNamespace(tree=str(args.tree), data_root=args.data_root, save=args.save,
                        ini=args.ini, exe=str(exe), port=7790, fullscreen=False,
                        potato=False, ram_mb=0, vram_mb=0, visible=False,
                        cache_reset=None, duration=args.duration, look_wait=30,
                        no_trace=False, sample=0, sample_us=2000, settle=8,
                        top=3, trace_from_boot=False)
    with (args.out / 'results.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, routes.FIELDS, extrasaction='ignore')
        writer.writeheader()
        for route in (r for r in routes.ROUTES if r[0] in args.routes):
            folder = args.out / route[0]
            folder.mkdir(exist_ok=True)
            if routes.others(a.tree):
                raise RuntimeError('Another game is running')
            proc, log = routes.launch(a, str(folder), '2560x1440', 0,
                ['SAVAGEWIND_TEST_WARP=' + route[1], 'SAVAGEWIND_VK_CPU_BENCH=1',
                 'SAVAGEWIND_VK_CPU_OPTS=0', 'SAVAGEWIND_VK_PROF=2'])
            game = None
            try:
                game = routes.Game(a.port)
                routes.wait_world(game, proc, 240)
                # Establish any route-specific hop and warm the loaded scene.
                if route[5]:
                    game.cmd('teleport', *route[5])
                    if game.poll(['teleport', 'status'], ('done', 'failed', 'idle'), 30) != 'done':
                        raise RuntimeError('Route hop failed')
                    game.cmd('fly', 'on')
                fixed_route = route[:5] + (None,)
                for round_index in range(args.rounds):
                    order = run_order(candidates, round_index)
                    for label, mask in order:
                        reply = game.cmd('cpuopts', mask)
                        if reply.get('mask') != mask:
                            raise RuntimeError('CPU switch was not applied')
                        tag = 'r{}-{}-{}'.format(round_index+1, route[0], label)
                        a.settle = 8 if round_index == 0 else 3
                        print('START ' + tag, flush=True)
                        result = routes.measure(game, a, routes.game_log(a, str(folder)),
                                                fixed_route, str(folder), tag)
                        others = routes.others(a.tree)
                        if others:
                            raise RuntimeError('Another game started during measurement: ' + str(others))
                        result.update(config=label, size='2560x1440', run=round_index+1, others=0)
                        writer.writerow(result)
                        stream.flush()
                        print('{}: {:.2f} fps, {:.3f} ms, renderer {:.3f} ms, p99 {:.3f} ms'.format(
                            tag, result['fps_avg'], result['frame_ms_avg'], result['cpu_render_ms'],
                            result['p99_ms']), flush=True)
            finally:
                routes.stop_game(game, proc, log)
    print('DONE: ' + str(args.out), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tree', type=Path, required=True)
    ap.add_argument('--data-root', required=True)
    ap.add_argument('--save', required=True)
    ap.add_argument('--ini', required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--baseline-exe', type=Path)
    ap.add_argument('--exe', type=Path, help='candidate executable; defaults to <tree>/build/savagewind[.exe]')
    ap.add_argument('--live', action='store_true', help='switch masks in one loaded game per route')
    ap.add_argument('--rounds', type=int, default=3)
    ap.add_argument('--duration', type=float, default=20)
    ap.add_argument('--routes', nargs='+', default=['plateau', 'castle'])
    ap.add_argument('--configs', nargs='+', default=['control', 'binding', 'writes', 'driver', 'all'])
    args = ap.parse_args()
    candidates = [('control', 0), ('binding', 1), ('writes', 2), ('driver', 4), ('all', 7), ('binding_writes', 3)]
    unknown = set(args.configs) - {n for n, _ in candidates}
    if unknown or not args.configs or args.rounds < 1 or args.duration < 5:
        ap.error('Use known configurations, at least one round and at least five seconds per route')
    candidates = [(n, m) for n, m in candidates if n in args.configs]
    if args.baseline_exe:
        if args.live:
            ap.error('--live uses one candidate binary; measure the installed baseline separately')
        candidates.insert(0, ('installed', None))
    args.out.mkdir(parents=True, exist_ok=True)
    exe = args.exe or args.tree / 'build' / ('savagewind.exe' if sys.platform == 'win32' else 'savagewind')
    manifest = {'args': {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                'candidate_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'ini_sha256': hashlib.sha256(Path(args.ini).read_bytes()).hexdigest()}
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    if args.live:
        return live(args, candidates, exe)
    for round_index in range(args.rounds):
        order = run_order(candidates, round_index)
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
