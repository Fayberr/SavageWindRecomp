#!/bin/bash
# waits until the A/B status says EXIT (max ~9.5 min per call), prints the per-boot lines so far
for i in $(seq 1 14); do
  s=$(ssh -o ConnectTimeout=10 derfa@192.168.178.3 'type C:\Users\derfa\swr-check\maxfps\pgo\ab-status.txt' 2>/dev/null | tr -d '\r' | head -1)
  case "$s" in EXIT*|FAILED*) break;; esac
  sleep 30
done
echo "status: $s"
ssh -o ConnectTimeout=10 derfa@192.168.178.3 'type X:\.Development\swr-wt\max-fps-pgo\ab\ab.log' 2>/dev/null | tr -d '\r' | grep -E "^==|fps|exit|rror|fail" | tail -30
