#!/usr/bin/env bash
# Ubuntu 24.04 contributor toolchain. Keep platform dependencies inside ignored build/.
set -euo pipefail
cd "$(dirname "$0")/.."
jobs=${JOBS:-2}
sudo apt-get install -y --no-install-recommends clang lld make cmake ninja-build pkg-config \
    python3-venv curl ca-certificates libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
    libvulkan-dev libshaderc-dev glslc spirv-tools vulkan-tools libx11-dev libxext-dev \
    libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
    libasound2-dev libwayland-dev libxkbcommon-dev
python3 -m venv tools/venv
tools/venv/bin/python -m pip install --no-cache-dir -r contrib/requirements.txt

# Same SDL release and checksum as tools/linux/build_deps.sh at the setup baseline.
prefix="$PWD/build/contrib/deps"
archive=build/contrib/SDL3-3.4.16.tar.gz
mkdir -p build/contrib
if [ ! -f "$prefix/lib/libSDL3.so" ]; then
    curl -fL --retry 3 https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz -o "$archive"
    echo "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68  $archive" | sha256sum -c -
    tar -xzf "$archive" -C build/contrib
    cmake -S build/contrib/SDL3-3.4.16 -B build/contrib/sdl-build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cmake --build build/contrib/sdl-build -j "$jobs"
    cmake --install build/contrib/sdl-build
fi
# Noble's Vulkan 1.3 headers predate dynamic-rendering local reads used by current test.
# Install headers only; continue using the system Vulkan loader and driver.
if [ ! -f "$prefix/include/vulkan/vulkan.h" ]; then
    archive=build/contrib/Vulkan-Headers-1.4.350.1.tar.gz
    curl -fL --retry 3 https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.350.1.tar.gz -o "$archive"
    echo "6d1bb65e49520344cc0a48af3dc02e993781efff14c7ebdcb8ae9fa23ddf7e83  $archive" | sha256sum -c -
    mkdir -p build/contrib/vulkan-headers "$prefix/include"
    tar -xzf "$archive" -C build/contrib/vulkan-headers --strip-components=1
    cp -R build/contrib/vulkan-headers/include/. "$prefix/include/"
fi
printf '\nToolchain ready. Run: contrib/check.sh\n'
