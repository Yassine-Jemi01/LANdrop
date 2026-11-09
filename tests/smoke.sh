#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN="$ROOT/build/landrop-net"
TMP=$(mktemp -d)
PORT=${LANDROP_TEST_PORT:-39571}
cleanup() {
    if [ -n "${SERVER_PID:-}" ]; then kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM
mkdir "$TMP/dest"
dd if=/dev/urandom of="$TMP/source.bin" bs=1024 count=128 2>/dev/null
"$BIN" receive "$PORT" "$TMP/dest" >"$TMP/receiver.log" 2>&1 &
SERVER_PID=$!
sleep 0.2
"$BIN" send 127.0.0.1 "$PORT" "$TMP/source.bin"
wait "$SERVER_PID"
SERVER_PID=
cmp "$TMP/source.bin" "$TMP/dest/source.bin"
echo "PASS: 128 KiB TCP transfer matches byte-for-byte"
