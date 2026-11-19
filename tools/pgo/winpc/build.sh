#!/bin/bash
# Phase 1: generate the game code while the runtime builds. Phase 2: everything else (game library, link).
export PATH="/c/Program Files/Git/cmd:$PATH"
OUT=/c/Users/derfa/swr-check/maxfps
cd /x/.Development/swr-wt/max-fps || exit 1
F="RELEASE=1 PGO=use LTO=thin PGO_PROFILE=/c/Users/derfa/swr-check/maxfps/fresh.profdata"
if grep -q 'gen: 0' $OUT/phase1.txt 2>/dev/null; then
  echo 'gen skipped: a complete generation from the same tools is already there' > $OUT/gen.log
  GENPID=""
else
  make gen > $OUT/gen.log 2>&1 &
  GENPID=$!
fi
make -j8 $F build/savagewind.exe build/savagewind-config.exe > $OUT/runtime.log 2>&1
RT=$?
GEN=0
[ -n "$GENPID" ] && { wait $GENPID; GEN=$?; }
echo "runtime exe: $RT, gen: $GEN" > $OUT/phase1.txt
[ $RT -eq 0 ] && [ $GEN -eq 0 ] || exit 1
make -j16 $F > $OUT/build.log 2>&1
exit $?
