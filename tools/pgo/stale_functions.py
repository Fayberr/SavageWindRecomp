#!/usr/bin/env python3
"""Which functions a PGO=use build silently compiles without profile data.

clang only says "of N functions, M have mismatched data that will be ignored" per file. A function whose body
changed since the profile was trained loses its counters, and when that is a hot function (draw_, exec_list) the
build quietly loses much of what PGO gave. This lists them, hottest first.

  # 1. the profile's functions and hashes
  llvm-profdata show --all-functions tools/pgo/runtime.profdata > prof.txt
  # 2. the current source's hashes: the file's usual flags, plus these (the object is not used)
  clang <RTFLAGS> -O0 -w -fprofile-instr-generate -S -emit-llvm -o exec.ll runtime/nvn/vulkan/exec.c
  # 3.
  python3 tools/pgo/stale_functions.py prof.txt exec.ll [more.ll ...]

Use the clang the profile is for (llvm-profdata and clang from the same toolchain).
"""
import os
import re
import sys


def read_profile(path):
    hashes, counts, cur = {}, {}, None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'^  (\S.*):$', line)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r'^    Hash: (0x[0-9a-fA-F]+)', line)
        if m and cur:
            hashes.setdefault(cur, set()).add(int(m.group(1), 16))
            continue
        m = re.match(r'^    Function count: (\d+)', line)
        if m and cur:
            counts[cur] = max(counts.get(cur, 0), int(m.group(1)))
    return hashes, counts


def read_ll(path):
    src = None
    out = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        if src is None:
            m = re.match(r'^source_filename = "(.*)"', line)
            if m:
                src = os.path.basename(m.group(1))
        m = re.match(r'^@__profd_(\S+) = .*?\{ i64 (-?\d+), i64 (-?\d+),', line)
        if m:
            name = m.group(1).strip('"')
            # a static function's profile name is "<file>:<name>", its symbol "<file>_<name>"
            if src and name.startswith(src + '_'):
                name = src + ':' + name[len(src) + 1:]
            out[name] = int(m.group(3)) & (2**64 - 1)
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    hashes, counts = read_profile(sys.argv[1])
    stale, absent, total = [], 0, 0
    for ll in sys.argv[2:]:
        for name, h in read_ll(ll).items():
            total += 1
            if name not in hashes:
                absent += 1
            elif h not in hashes[name]:
                stale.append((counts.get(name, 0), name))
    print(f'{total} functions, {len(stale)} with a stale profile (ignored), {absent} not in the profile')
    for count, name in sorted(stale, reverse=True):
        print(f'{count:14d}  {name}')
    return 1 if stale else 0


if __name__ == '__main__':
    sys.exit(main())
