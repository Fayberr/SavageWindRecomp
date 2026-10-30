"""Tests of tools/swprof.py on a capture built by hand. `make test-prof` or `python3 -m unittest tools.test_swprof`."""
import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import swprof  # noqa: E402

TPM = 1000  # ticks per ms: the header's two clocks run 1000 ticks per 1e6 ns


def build(sites, threads, records):
    """sites {id: name}; threads [name]; records [(thread, t0, t1, val, site, kind)] in ticks."""
    body = b''
    for sid, name in sites.items():
        n = name.encode()
        body += struct.pack('<IH', sid, len(n)) + n
    for i, name in enumerate(threads):
        body += swprof.THREAD.pack(i, 0, 100 + i, name.encode())
    by_thread = {}
    for r in records:
        by_thread.setdefault(r[0], []).append(r)
    chunks = b''
    for th, recs in by_thread.items():
        chunks += swprof.CHUNK.pack(th, len(recs))
        for _, a, b, val, site, kind in recs:
            chunks += swprof.REC.pack(a, b, val, site, kind)
    end_ticks = max(r[2] for r in records) + 10
    header = swprof.HEADER.pack(b'SWPROF01', 1, swprof.REC.size, 0, 0, end_ticks, end_ticks * 1000000 // TPM,
                                1700000000, len(sites), len(threads), len(by_thread), 0)
    return header + body + chunks


class SwprofTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        sites = {1: 'render', 2: 'draw', 3: 'exec', 4: 'exec/pre', 5: 'queue'}
        recs = []
        # 10 frames of 10 ms, one of them (frame 5) 60 ms because "draw" takes 50 ms in it
        t = 0
        for f in range(11):
            recs.append((0, t * TPM, 0, f, 0, swprof.FRAME))
            dur = 60 if f == 5 else 10
            if f < 10:
                recs.append((0, t * TPM, (t + dur) * TPM, 0, 1, swprof.ZONE))
                inner = 50 if f == 5 else 4
                recs.append((0, (t + 1) * TPM, (t + 1 + inner) * TPM, 0, 2, swprof.ZONE))
                recs.append((1, t * TPM, 7, 0, 5, swprof.COUNTER))
                # 100 calls of exec, 2 ms in total, 1.5 ms of it in exec/pre
                recs.append((1, 0, 2 * TPM, f, 3, swprof.ACC | (100 << 8)))
                recs.append((1, 0, int(1.5 * TPM), f, 4, swprof.ACC | (100 << 8)))
            t += dur
        self.path = os.path.join(self.dir.name, 'x.swprof')
        with open(self.path, 'wb') as fh:
            fh.write(build(sites, ['main', 'render'], recs))

    def test_reads_the_file(self):
        cap = swprof.Capture(self.path)
        self.assertEqual([n for n, _ in cap.threads], ['main', 'render'])
        self.assertEqual(len(cap.frame_marks), 11)
        self.assertAlmostEqual(cap.ticks_per_ms, TPM, delta=1)
        self.assertEqual(len(cap.zones[0]), 20)

    def test_frames_and_slow_frame_cause(self):
        res = swprof.analyse(swprof.Capture(self.path))
        f = res['frames']
        self.assertEqual(f['count'], 10)
        self.assertAlmostEqual(f['ms_max'], 60, delta=0.1)
        self.assertAlmostEqual(f['ms_p50'], 10, delta=0.1)
        self.assertEqual(f['over_50_ms'], 1)
        slow = res['slow_frames'][0]
        self.assertEqual(slow['frame'], 5)
        self.assertEqual(slow['top'][0]['name'], 'render/draw')
        self.assertAlmostEqual(slow['top'][0]['ms'], 50, delta=0.1)

    def test_zone_self_time_and_acc(self):
        res = swprof.analyse(swprof.Capture(self.path))
        main = next(t for t in res['threads'] if t['name'] == 'main')
        render = next(z for z in main['zones'] if z['path'] == 'render')
        draw = next(z for z in main['zones'] if z['path'] == 'render/draw')
        self.assertAlmostEqual(render['incl_ms'], (9 * 10 + 60) / 10, delta=0.01)
        self.assertAlmostEqual(draw['incl_ms'], (9 * 4 + 50) / 10, delta=0.01)
        self.assertAlmostEqual(render['self_ms'], render['incl_ms'] - draw['incl_ms'], delta=0.01)
        rt = next(t for t in res['threads'] if t['name'] == 'render')
        ex = next(a for a in rt['acc'] if a['path'] == 'exec')
        pre = next(a for a in rt['acc'] if a['path'] == 'exec/pre')
        self.assertAlmostEqual(ex['incl_ms'], 2.0, delta=0.01)
        self.assertAlmostEqual(ex['self_ms'], 0.5, delta=0.01)
        self.assertAlmostEqual(pre['calls_per_frame'], 100, delta=0.1)
        top = {(t['thread'], t['name']): t['self_ms_per_frame'] for t in res['top_self']}
        self.assertAlmostEqual(top[('render', 'exec/pre')], 1.5, delta=0.01)

    def test_counters(self):
        res = swprof.analyse(swprof.Capture(self.path))
        self.assertEqual(res['counters']['queue']['max'], 7)

    def test_frame_range(self):
        res = swprof.analyse(swprof.Capture(self.path), 6, 9)
        self.assertEqual(res['frames']['count'], 4)
        self.assertAlmostEqual(res['frames']['ms_max'], 10, delta=0.1)

    def test_cli_outputs(self):
        js, csv = os.path.join(self.dir.name, 'o.json'), os.path.join(self.dir.name, 'o.csv')
        with contextlib.redirect_stdout(io.StringIO()):
            swprof.main([self.path, '--json', js, '--csv', csv, '--top', '3'])
        with open(js) as fh:
            data = json.load(fh)
        self.assertEqual(data['frames']['count'], 10)
        with open(csv) as fh:
            lines = fh.read().splitlines()
        self.assertEqual(len(lines), 11)
        self.assertTrue(lines[0].startswith('frame,ms,'))

    def test_rejects_other_files(self):
        bad = os.path.join(self.dir.name, 'bad')
        with open(bad, 'wb') as fh:
            fh.write(b'\0' * 200)
        with self.assertRaises(SystemExit):
            swprof.Capture(bad)


if __name__ == '__main__':
    unittest.main()
