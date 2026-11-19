#!/bin/bash
# waits until the PGO task status says EXIT or FAILED (max ~9.5 min per call)
for i in $(seq 1 19); do
  s=$(ssh -o ConnectTimeout=10 derfa@192.168.178.3 'type C:\Users\derfa\swr-check\maxfps\pgo\status.txt' 2>/dev/null | tr -d '\r' | head -1)
  case "$s" in EXIT*|FAILED*) echo "status: $s"; exit 0;; esac
  sleep 30
done
echo "status: still $s"
