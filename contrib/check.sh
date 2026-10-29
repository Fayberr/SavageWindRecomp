#!/usr/bin/env bash
# Source-only checks: no game files, saves or physical controller required.
set -euo pipefail
cd "$(dirname "$0")/.."
jobs=${JOBS:-2}
source contrib/env.sh
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        py=tools/venv/Scripts/python.exe
        ;;
    Linux)
        py=tools/venv/bin/python
        ;;
    *) echo 'This helper supports Linux and MSYS2 UCRT64.' >&2; exit 2 ;;
esac
mkdir -p build/contrib
"$py" tools/test_game_sites.py
"$py" tools/test_layout.py
make -j "$jobs" test-bind-capture test-vibration
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        # The standalone upstream target omits pread; linking windows.c also needs its runtime dependencies.
        # Keep that failing harness visible in VERIFICATION.md; the full runtime below links windows.c normally.
        printf '\nNot run on Windows: standalone RomFS test needs a platform harness fix (see contrib/VERIFICATION.md).\n'
        ;;
    *) make build/test_romfs && ./build/test_romfs ;;
esac
make -j "$jobs"
printf '\nSupported source checks and runtime build passed. Gameplay still needs a separate test.\n'
