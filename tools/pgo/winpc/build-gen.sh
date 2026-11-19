#!/bin/bash
export PATH="/c/Program Files/Git/cmd:$PATH"
cd /x/.Development/swr-wt/max-fps || exit 1
make -j16 RELEASE=1 PGO=gen LTO=thin B=build-gen build-gen/savagewind.exe > /c/Users/derfa/swr-check/maxfps/pgo/build-gen.log 2>&1
exit $?
