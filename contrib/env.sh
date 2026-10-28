# Source from bash to use the host's contributor dependencies, including for launching build/savagewind.
contrib_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH" ;;
    Linux)
        export CPATH="$contrib_root/build/contrib/deps/include${CPATH:+:$CPATH}"
        export LIBRARY_PATH="$contrib_root/build/contrib/deps/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
        export LD_LIBRARY_PATH="$contrib_root/build/contrib/deps/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        export PKG_CONFIG_PATH="$contrib_root/build/contrib/deps/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
        ;;
esac
unset contrib_root
