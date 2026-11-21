#!/bin/sh
# Run in a Linux container with --network none. Reuse one DLL connection for
# GET and HEAD so a stale callback from the first request is exercised.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
cd "$root"
unset HTTPS_PROXY ALL_PROXY HTTP_PROXY https_proxy all_proxy http_proxy
cc -g -fsanitize=address,undefined -fno-pie -no-pie -Wno-deprecated-declarations -Isource \
    tests/network_diag_offline.c source/horizon_dlls_curl.c source/horizon_dlls.c \
    source/steam/steam_probe.c source/steam/steam_directory.c \
    -lcurl -lcrypto -lzstd -lz -lpthread -o "$build/offline"
if ! "$build/offline" >"$build/log" 2>&1; then cat "$build/log"; exit 1; fi
cat "$build/log"
for stage in dll.manifest dll.bundle.head steam.directory; do
    grep -F "stage=$stage curl=6 " "$build/log" | grep -q 'http=0'
done
if grep -E 'https?://|Authorization:' "$build/log"; then exit 1; fi
