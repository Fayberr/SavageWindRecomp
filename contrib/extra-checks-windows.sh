#!/usr/bin/env bash
# Synthetic shader and SIMD samples under MSYS2 UCRT64, with no game files required.
set -euo pipefail
cd "$(dirname "$0")/.."
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo 'Run this helper under MSYS2 UCRT64.' >&2; exit 2 ;; esac
source contrib/env.sh
export PYTHONUTF8=1
tools/test_translate.sh 2000 2 glsl
tools/venv/Scripts/python.exe tools/test_simd.py --encodings 1000 --cases 4 --jobs 2 --no-validity
