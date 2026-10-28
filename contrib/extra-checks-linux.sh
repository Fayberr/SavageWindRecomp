#!/usr/bin/env bash
# Small, reproducible samples of upstream's synthetic shader and portable SIMD tests.
set -euo pipefail
cd "$(dirname "$0")/.."
source contrib/env.sh
mkdir -p build/contrib
tools/test_translate.sh 2000 2 glsl

# Upstream's SIMD harness assumes implicit simd.h on x86 and omits Linux libm.
# Force the headers in order on ARM and add libm at link time without changing upstream tests.
cat > build/contrib/clang-simd <<'EOF'
#!/usr/bin/env bash
for arg in "$@"; do
    if [ "$arg" = -c ]; then exec clang "$@"; fi
done
exec clang "$@" -lm
EOF
chmod +x build/contrib/clang-simd
CC="$PWD/build/contrib/clang-simd" tools/venv/bin/python tools/test_simd.py \
    --encodings 1000 --cases 4 --jobs 2 --no-validity --cflags='-include cpu.h -include simd.h'
