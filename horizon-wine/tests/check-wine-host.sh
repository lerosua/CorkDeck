#!/bin/sh
# Host checks for the Wine primitives changed by the Switch compatibility work.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$repo"
for check in horizon_duplex_pipe horizon_thread_handles horizon_async \
             horizon_async_sockets horizon_fd_owner select_wait window_destroy wow64_unix_tables; do
    python3 "horizon-wine/tests/check_$check.py"
done
