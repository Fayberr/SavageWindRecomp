#!/usr/bin/env python3
"""Reads a capture of the on-demand profiler (runtime/prof.c) and says where the time went.

Usage:
  swprof.py [capture.swprof] [--top N] [--slow MS] [--json FILE] [--csv FILE] [--frames FIRST:LAST]

Without a file the newest profile/swprof-*.swprof is used. Python 3.8, standard library only.

The report has: the frames (rate, percentiles, the slow ones with what each thread was doing), the zones
(SW_ZONE) as a tree per thread, the added-up work (SW_ACC) as a tree per thread, the counters, and the top places by
self time. Times are milliseconds per frame unless a heading says otherwise.
"""
import argparse
import bisect
import collections
import glob
import json
import math
import os
import struct
import sys

HEADER = struct.Struct('<8sIIQQQQQIIII')
THREAD = struct.Struct('<IIQ32s')
CHUNK = struct.Struct('<II')
REC = struct.Struct('<QQQII')

ZONE, COUNTER, FRAME, ACC, GPU = 1, 2, 3, 4, 5
GPU_IDLE = '(gpu idle)'


class Capture:
    """sites {id: name}, threads [name], zones/acc per thread, counters, frame marks; all times in ms from the start."""

    def __init__(self, path):
        with open(path, 'rb') as f:
            data = f.read()
        (magic, version, recsize, t0, n0, t1, n1, unix0, nsites, nthreads, nchunks, _) = HEADER.unpack_from(data, 0)
        if magic != b'SWPROF01' or version != 1 or recsize != REC.size:
            raise SystemExit(f'{path}: not a capture this tool reads (magic {magic!r}, version {version})')
        self.path = path
        self.unix_start = unix0
        pos = HEADER.size
        self.sites = {}
        for _ in range(nsites):
            sid, ln = struct.unpack_from('<IH', data, pos)
            pos += 6
            self.sites[sid] = data[pos:pos + ln].decode('utf-8', 'replace')
            pos += ln
        self.threads = []
        for _ in range(nthreads):
            _, _, tid, name = THREAD.unpack_from(data, pos)
            pos += THREAD.size
            self.threads.append((name.split(b'\0')[0].decode('utf-8', 'replace') or 'thread', tid))
        span_ns = max(n1 - n0, 1)
        self.ticks_per_ms = max((t1 - t0) / span_ns * 1e6, 1e-9)
        self.ticks0, self.ticks1 = t0, t1
        self.duration_ms = span_ns / 1e6
        # per thread: lists of (start, end, site) zones and (frame, site, calls, ticks) accumulations
        self.zones = [[] for _ in self.threads]
        self.acc = [[] for _ in self.threads]
        self.counters = collections.defaultdict(list)  # name -> [(ms, value)]
        self.frame_marks = []  # (ms, frame number)
        self.gpu = []  # (start, end, site, draws)
        self.records = 0
        ms = lambda t: (t - t0) / self.ticks_per_ms
        for _ in range(nchunks):
            thread, n = CHUNK.unpack_from(data, pos)
            pos += CHUNK.size
            for a, b, val, site, kind in REC.iter_unpack(data[pos:pos + n * REC.size]):
                k = kind & 0xff
                if a < t0:
                    continue  # began before the capture
                if k == ZONE and thread < len(self.zones):
                    self.zones[thread].append((ms(a), ms(b), site))
                elif k == ACC and thread < len(self.acc):
                    self.acc[thread].append((val, site, kind >> 8, b / self.ticks_per_ms))
                elif k == COUNTER:
                    self.counters[self.sites.get(site, '?')].append((ms(a), b))  # the value is in t1
                elif k == FRAME:
                    self.frame_marks.append((ms(a), val))
                elif k == GPU:
                    self.gpu.append((ms(a), ms(b), site, val & 0xffffffff))
            pos += n * REC.size
            self.records += n
        self.frame_marks.sort()
        self.gpu.sort()
        if self.gpu:  # the GPU's rows are a thread of their own: flat, one after another
            self.threads.append(('gpu', 0))
            self.zones.append([(a, b, site) for a, b, site, _ in self.gpu])
            self.acc.append([])


def percentile(sorted_values, p):
    """Nearest rank."""
    if not sorted_values:
        return 0.0
    i = max(0, min(len(sorted_values) - 1, math.ceil(p / 100 * len(sorted_values)) - 1))
    return sorted_values[i]


class Frames:
    def __init__(self, cap, first=None, last=None):
        marks = cap.frame_marks
        self.starts = [m[0] for m in marks]
        self.numbers = [m[1] for m in marks]
        # frame i lasts from mark i to mark i+1; the last mark has no end
        self.items = []
        for i in range(len(marks) - 1):
            n = marks[i][1]
            if (first is not None and n < first) or (last is not None and n > last):
                continue
            self.items.append((i, n, marks[i][0], marks[i + 1][0] - marks[i][0]))
        self.ms = sorted(it[3] for it in self.items)

    def index_at(self, t):
        """Frame mark index whose frame holds time t (-1 before the first mark)."""
        return bisect.bisect_right(self.starts, t) - 1


def build_tree(zones, names):
    """zones: (start, end, site) of one thread -> {path tuple: [calls, inclusive ms, self ms, max ms]}; also each
    zone's (start, self, path) for attributing to frames."""
    zones = sorted(zones, key=lambda z: (z[0], -z[1]))
    stack = []  # [end, path, child ms, start, dur]
    stats = {}
    events = []

    def close(item):
        end, path, child, start, dur = item
        s = stats.setdefault(path, [0, 0.0, 0.0, 0.0])
        s[0] += 1
        s[1] += dur
        s[2] += max(dur - child, 0.0)
        s[3] = max(s[3], dur)
        events.append((start, max(dur - child, 0.0), path))

    for start, end, site in zones:
        while stack and start >= stack[-1][0] - 1e-9:
            close(stack.pop())
        name = names.get(site, '?')
        path = (stack[-1][1] if stack else ()) + (name,)
        if stack:
            stack[-1][2] += end - start
        stack.append([end, path, 0.0, start, end - start])
    while stack:
        close(stack.pop())
    return stats, events


def acc_tree(records, names):
    """records: (frame, site, calls, ms) -> {name: [calls, ms]} with names as '/' paths."""
    out = {}
    for _, site, calls, ms in records:
        s = out.setdefault(names.get(site, '?'), [0, 0.0])
        s[0] += calls
        s[1] += ms
    return out


def self_times(tree):
    """{path: [calls, incl]} where path is a '/' name -> {path: self ms}: inclusive minus the direct children's."""
    child = collections.defaultdict(float)
    for p, (_, ms) in tree.items():
        if '/' in p:
            child[p.rsplit('/', 1)[0]] += ms
    return {p: max(ms - child.get(p, 0.0), 0.0) for p, (_, ms) in tree.items()}


def analyse(cap, first=None, last=None):
    fr = Frames(cap, first, last)
    nframes = max(len(fr.items), 1)
    result = {'file': os.path.basename(cap.path), 'duration_s': round(cap.duration_ms / 1000, 3), 'events': cap.records,
              'frames': {}, 'threads': [], 'counters': {}, 'top_self': [], 'slow_frames': []}
    if fr.items:
        ms = fr.ms
        total = sum(ms)
        result['frames'] = {
            'count': len(ms), 'fps_avg': round(1000 * len(ms) / total, 2) if total else 0,
            'ms_avg': round(total / len(ms), 3), 'ms_p50': round(percentile(ms, 50), 3),
            'ms_p95': round(percentile(ms, 95), 3), 'ms_p99': round(percentile(ms, 99), 3), 'ms_max': round(ms[-1], 3),
            'over_33_ms': sum(1 for v in ms if v > 33.4), 'over_50_ms': sum(1 for v in ms if v > 50)}
    span = (fr.items[0][2], fr.items[-1][2] + fr.items[-1][3]) if fr.items else (0.0, cap.duration_ms)

    wanted = {it[1] for it in fr.items}
    per_frame_self = collections.defaultdict(lambda: collections.defaultdict(float))  # frame mark index -> key -> ms
    top = collections.defaultdict(lambda: [0, 0.0])  # (thread, name) -> calls, self ms
    for ti, (tname, tid) in enumerate(cap.threads):
        zones = [z for z in cap.zones[ti] if span[0] <= z[0] < span[1]]
        stats, events = build_tree(zones, cap.sites)
        for start, selfms, path in events:
            i = fr.index_at(start)
            per_frame_self[i][(tname, '/'.join(path))] += selfms
        accs = [a for a in cap.acc[ti] if a[0] in wanted] if fr.items else cap.acc[ti]
        at = acc_tree(accs, cap.sites)
        aself = self_times(at)
        for p, selfms in aself.items():
            top[(tname, p)][0] += at[p][0]
            top[(tname, p)][1] += selfms
        for path, (calls, incl, selfms, mx) in stats.items():
            top[(tname, '/'.join(path))][0] += calls
            top[(tname, '/'.join(path))][1] += selfms
        busy = sum(v[1] for p, v in stats.items() if len(p) == 1)
        result['threads'].append({
            'name': tname, 'os_id': tid, 'busy_ms_per_frame': round(busy / nframes, 3),
            'zones': [{'path': '/'.join(p), 'calls_per_frame': round(v[0] / nframes, 2), 'incl_ms': round(v[1] / nframes, 4),
                       'self_ms': round(v[2] / nframes, 4), 'max_ms': round(v[3], 3)} for p, v in sorted(stats.items())],
            'acc': [{'path': p, 'calls_per_frame': round(v[0] / nframes, 1), 'incl_ms': round(v[1] / nframes, 4),
                     'self_ms': round(aself[p] / nframes, 4)} for p, v in sorted(at.items())]})
    for name, vals in cap.counters.items():
        vs = [v for t, v in vals if span[0] <= t < span[1]]
        if vs:
            result['counters'][name] = {'min': min(vs), 'avg': round(sum(vs) / len(vs), 2), 'max': max(vs), 'samples': len(vs)}
    result['top_self'] = [{'thread': k[0], 'name': k[1], 'calls_per_frame': round(v[0] / nframes, 2),
                           'self_ms_per_frame': round(v[1] / nframes, 4)}
                          for k, v in sorted(top.items(), key=lambda kv: -kv[1][1])]
    # slow frames: the worst few, with the places that took the most self time inside each
    worst = sorted(fr.items, key=lambda it: -it[3])[:10]
    for i, n, start, dur in worst:
        cause = sorted(per_frame_self.get(i, {}).items(), key=lambda kv: -kv[1])[:6]
        result['slow_frames'].append({'frame': n, 'ms': round(dur, 2), 'at_s': round(start / 1000, 3),
                                      'top': [{'thread': k[0], 'name': k[1], 'ms': round(v, 2)} for k, v in cause]})
    result['gpu'] = gpu_summary(cap, fr, span, nframes)
    result['_frames'] = fr
    result['_per_frame_self'] = per_frame_self
    return result


def gpu_summary(cap, fr, span, nframes):
    if not cap.gpu:
        return {}
    rows = collections.defaultdict(lambda: [0, 0.0, 0, 0.0])  # name -> rows, ms, draws, longest
    busy = idle = 0.0
    for a, b, site, draws in cap.gpu:
        if not (span[0] <= a < span[1]):
            continue
        name = cap.sites.get(site, '?')
        r = rows[name]
        r[0] += 1
        r[1] += b - a
        r[2] += draws
        r[3] = max(r[3], b - a)
        if name == GPU_IDLE:
            idle += b - a
        else:
            busy += b - a
    return {'busy_ms_per_frame': round(busy / nframes, 3), 'idle_ms_per_frame': round(idle / nframes, 3),
            'busy_percent': round(100 * busy / (busy + idle), 1) if busy + idle else 0,
            'rows': [{'name': n, 'rows_per_frame': round(v[0] / nframes, 2), 'ms_per_frame': round(v[1] / nframes, 4),
                      'draws_per_frame': round(v[2] / nframes, 1), 'max_ms': round(v[3], 3)}
                     for n, v in sorted(rows.items(), key=lambda kv: -kv[1][1])]}


def text_report(res, top_n, slow_ms):
    out = []
    w = out.append
    w(f"Capture {res['file']}: {res['duration_s']} s, {res['events']} events")
    f = res['frames']
    if f:
        w(f"\nFrames: {f['count']}, {f['fps_avg']} fps average; frame time ms: avg {f['ms_avg']}, p50 {f['ms_p50']}, "
          f"p95 {f['ms_p95']}, p99 {f['ms_p99']}, max {f['ms_max']}; over 33 ms: {f['over_33_ms']}, over 50 ms: {f['over_50_ms']}")
    else:
        w('\nNo frames in this capture (prof_frame was not called).')
    w(f"\nTop {top_n} places by self time (ms per frame, calls per frame):")
    for t in res['top_self'][:top_n]:
        w(f"  {t['self_ms_per_frame']:8.3f}  {t['calls_per_frame']:9.1f}x  [{t['thread']}] {t['name']}")
    g = res.get('gpu')
    if g:
        w(f"\nGPU: busy {g['busy_ms_per_frame']} ms per frame ({g['busy_percent']}% of the time), idle {g['idle_ms_per_frame']} ms. "
          f"Passes, dispatches and copies by GPU time (ms per frame):")
        for r in g['rows'][:top_n]:
            w(f"  {r['ms_per_frame']:8.3f}  {r['rows_per_frame']:7.2f}x  {r['draws_per_frame']:8.1f} draws  max {r['max_ms']:.2f}  {r['name']}")
    for th in res['threads']:
        if not th['zones'] and not th['acc'] or th['name'] == 'gpu':
            continue
        w(f"\nThread {th['name']} (busy in zones {th['busy_ms_per_frame']} ms per frame)")
        for z in th['zones']:
            if z['incl_ms'] < 0.01:
                continue
            depth = z['path'].count('/')
            w(f"  {'  ' * depth}{z['path'].rsplit('/', 1)[-1]:<{max(34 - 2 * depth, 8)}} incl {z['incl_ms']:8.3f}  self {z['self_ms']:8.3f}"
              f"  {z['calls_per_frame']:8.1f}x  max {z['max_ms']:.2f}")
        if th['acc']:
            w('  added-up work (per frame):')
            for a in th['acc']:
                if a['incl_ms'] < 0.005:
                    continue
                depth = a['path'].count('/')
                w(f"    {'  ' * depth}{a['path'].rsplit('/', 1)[-1]:<{max(30 - 2 * depth, 8)}} incl {a['incl_ms']:8.3f}  self {a['self_ms']:8.3f}"
                  f"  {a['calls_per_frame']:9.1f}x")
    if res['counters']:
        w('\nCounters:')
        for name, c in sorted(res['counters'].items()):
            w(f"  {name:<28} min {c['min']}  avg {c['avg']}  max {c['max']}  ({c['samples']} samples)")
    slow = [s for s in res['slow_frames'] if s['ms'] > slow_ms]
    if slow:
        w(f'\nSlowest frames (over {slow_ms:g} ms), with the most self time inside each:')
        for s in slow:
            w(f"  frame {s['frame']}: {s['ms']} ms at {s['at_s']} s")
            for t in s['top']:
                w(f"      {t['ms']:7.2f} ms  [{t['thread']}] {t['name']}")
    return '\n'.join(out)


def write_csv(res, path):
    fr = res['_frames']
    pfs = res['_per_frame_self']
    columns = [f"{t['thread']}:{t['name']}" for t in res['top_self'][:30] if t['self_ms_per_frame'] > 0]
    with open(path, 'w') as f:
        f.write('frame,ms,' + ','.join(c.replace(',', ';') for c in columns) + '\n')
        for i, n, start, dur in fr.items:
            row = pfs.get(i, {})
            f.write(f'{n},{dur:.3f},' + ','.join(f"{row.get(tuple(c.split(':', 1)), 0.0):.3f}" for c in columns) + '\n')


def newest():
    files = sorted(glob.glob('profile/swprof-*.swprof'), key=os.path.getmtime)
    return files[-1] if files else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('capture', nargs='?')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--slow', type=float, default=33.4, help='list frames over this many ms (default 33.4)')
    ap.add_argument('--json', metavar='FILE')
    ap.add_argument('--csv', metavar='FILE', help='one row per frame: its time and the self time of the top places')
    ap.add_argument('--frames', metavar='FIRST:LAST', help='only these frame numbers')
    a = ap.parse_args(argv)
    path = a.capture or newest()
    if not path:
        sys.exit('no capture given and no profile/swprof-*.swprof here')
    first = last = None
    if a.frames:
        lo, _, hi = a.frames.partition(':')
        first = int(lo) if lo else None
        last = int(hi) if hi else None
    res = analyse(Capture(path), first, last)
    print(text_report(res, a.top, a.slow))
    if a.csv:
        write_csv(res, a.csv)
    if a.json:
        with open(a.json, 'w') as f:
            json.dump({k: v for k, v in res.items() if not k.startswith('_')}, f, indent=1)


if __name__ == '__main__':
    main()
