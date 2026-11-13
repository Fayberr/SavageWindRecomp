#!/usr/bin/env python3
"""Synthetic boundary and sustained-throughput checks for fps_windows."""
import unittest
from fps_windows import summary, windows


def trace(durations):
    rows = [{'end_ms': 1234567., 'frame_ms': 87.}]
    end = rows[0]['end_ms']
    for duration in durations:
        end += duration
        rows.append({'end_ms': end, 'frame_ms': duration})
    return rows


class WindowsTest(unittest.TestCase):
    def test_exact_target_and_partial_tail(self):
        result = summary(trace([10.] * 205))
        self.assertEqual(result['seconds'], 2)
        self.assertEqual(result['at_least_100'], 2)
        self.assertTrue(result['passes'])

    def test_long_frame_spans_windows(self):
        self.assertEqual(windows(trace([2500.])), [.4, .4])

    def test_crossing_frame_is_split(self):
        self.assertEqual(windows(trace([600.] * 5)), [1 + 400 / 600, 200 / 600 + 1 + 200 / 600, 400 / 600 + 1])

    def test_high_average_does_not_hide_slow_second(self):
        result = summary(trace([5.] * 1800 + [20.] * 50))
        self.assertEqual(result['percent_at_least_100'], 90)
        self.assertEqual(result['minimum_fps'], 50)
        self.assertFalse(result['passes'])


if __name__ == '__main__':
    unittest.main()
