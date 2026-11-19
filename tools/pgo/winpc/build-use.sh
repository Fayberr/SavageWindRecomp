#!/bin/bash
# $1: profile (msys path), $2: build dir
export PATH="/c/Program Files/Git/cmd:$PATH"
cd /x/.Development/swr-wt/max-fps || exit 1
make -j16 RELEASE=1 PGO=use LTO=thin PGO_PROFILE="$1" B="$2" "$2/savagewind.exe" "$2/savagewind-config.exe" > /c/Users/derfa/swr-check/maxfps/pgo/build-use-$2.log 2>&1
exit $?
