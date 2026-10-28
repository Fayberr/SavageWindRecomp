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
make -j "$jobs" test-bind-capture test-vibration build/test_romfs
./build/test_romfs
make -j "$jobs"
printf '\nSource checks and runtime build passed. Gameplay still needs a separate test.\n'
