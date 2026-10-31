#!/bin/sh
set -eu
binary="${1:-./build/flashforge}"
root="${2:-/tmp/flashforge-slow-wal-demo}"
rm -rf "$root"
mkdir -p "$root"
dd if=/dev/urandom of="$root/input.bin" bs=4096 count=32 2>/dev/null

echo "=== Baseline ==="
"$binary" --data "$root/baseline" put "$root/input.bin"
"$binary" --data "$root/baseline" observe

echo "=== Injected 100 ms delay before every WAL fsync ==="
FLASHFORGE_FAULT_WAL_FSYNC_DELAY_MS=100 "$binary" --data "$root/slow" put "$root/input.bin"
"$binary" --data "$root/slow" diagnose

echo "=== Recovered (fault removed) ==="
"$binary" --data "$root/recovered" put "$root/input.bin"
"$binary" --data "$root/recovered" observe
