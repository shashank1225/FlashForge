#!/bin/sh
set -eu
out="${1:-samples}"
mkdir -p "$out"
dd if=/dev/urandom of="$out/common.bin" bs=4096 count=64 2>/dev/null
cp "$out/common.bin" "$out/file1.bin"
cp "$out/common.bin" "$out/file2.bin"
dd if=/dev/urandom of="$out/unique.bin" bs=4096 count=16 2>/dev/null
cat "$out/unique.bin" >> "$out/file2.bin"
cp "$out/common.bin" "$out/file3.bin"
cat "$out/unique.bin" >> "$out/file3.bin"
echo "Created duplicate-rich files in $out"
