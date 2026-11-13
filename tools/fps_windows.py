#!/usr/bin/env python3
"""Report complete one-second FPS windows from perf_routes frame traces."""
import argparse
import csv
import json
import math


def windows(rows):
    # The first traced frame can have started before tracing was enabled.
    rows = rows[1:]
    if not rows:
        return []
    start = rows[0]['end_ms'] - rows[0]['frame_ms']
    count = math.floor((rows[-1]['end_ms'] - start) / 1000)
    fps = [0.0] * count
    for row in rows:
        duration = row['frame_ms']
        if duration <= 0:
            raise ValueError('Frame duration must be positive')
        end = row['end_ms']
        begin = end - duration
        first = max(0, math.floor((begin - start) / 1000))
        last = min(count, math.floor((end - start) / 1000) + 1)
        for i in range(first, last):
            overlap = min(end, start + (i + 1) * 1000) - max(begin, start + i * 1000)
            fps[i] += max(0, overlap) / duration
    return fps


def summary(rows):
    fps = windows(rows)
    if not fps:
        raise ValueError('Trace has no complete one-second window')
    above = sum(f >= 100 - 1e-7 for f in fps)
    return {'seconds': len(fps), 'at_least_100': above,
            'percent_at_least_100': 100 * above / len(fps),
            'minimum_fps': min(fps), 'below_90': sum(f < 90 - 1e-7 for f in fps),
            'passes': above / len(fps) >= .9 and min(fps) >= 90 - 1e-7,
            'fps': fps}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('traces', nargs='+')
    args = ap.parse_args()
    for path in args.traces:
        with open(path, encoding='utf-8') as stream:
            rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(stream)]
        print(json.dumps({'trace': path, **summary(rows)}))


if __name__ == '__main__':
    main()
