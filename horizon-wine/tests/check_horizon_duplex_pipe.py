#!/usr/bin/env python3
"""Exercise the Horizon named-pipe request handlers and devoptab callbacks on the host."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
unix = root / "dlls/ntdll/unix"
source = (unix / "horizon.c").read_text()


def function(name):
    match = re.search(r"^(?:static )?[^;{}\n]*?\b" + name + r"\([^;{}]*?\)\n\{.*?^}", source, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match[0]


def structure(name):
    return re.search(r"^struct horizon_" + name + r"\n\{.*?^};", source, re.M | re.S)[0]


# Keep the wire structures and buffers identical to those compiled into the NRO.
defines = "\n".join(re.findall(r"^#define HORIZON_(?:STATUS_\w+|PIPE_BUFFER_SIZE|NAMED_PIPE_\w+) [^\n]+", source, re.M))
structures = "\n".join(map(structure, (
    "pipe", "pipe_file", "server_request_header", "server_reply_header", "object_attributes",
    "object_name", "create_named_pipe_request", "create_named_pipe_reply", "open_file_object_request",
    "open_file_object_reply",
)))
functions = "\n".join(map(function, (
    "horizon_set_reent_errno", "horizon_pipe_destroy", "horizon_pipe_release", "horizon_pipe_close_end",
    "horizon_pipe_close_r", "horizon_pipe_ring", "horizon_pipe_read_r", "horizon_pipe_write_r", "horizon_pipe_endpoint",
    "horizon_mojo_first_io", "horizon_pipe_alloc", "horizon_pipe", "horizon_duplex_pipe", "horizon_process_socketpair", "horizon_pipe_async_status",
    "horizon_server_queue_pipe_async", "horizon_named_pipe_has_prefix", "horizon_named_pipe_name_tail",
    "horizon_named_pipe_name_equal", "horizon_server_find_named_pipe_locked", "horizon_mojo_channel",
    "horizon_server_parse_object_attributes", "horizon_server_set_object_name",
    "horizon_server_handle_create_named_pipe", "horizon_server_handle_open_file_object",
)))
fixture = Path(__file__).with_name("horizon_duplex_pipe_host.c").read_text()
with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    src.write_text(fixture.replace("/* WIRE */", defines + "\n" + structures)
                   .replace("/* IMPLEMENTATION */", functions))
    subprocess.run(["cc", "-std=gnu11", "-g", "-fsanitize=address,undefined", "-pthread",
                    "-I", str(unix), str(src), "-o", str(exe)], check=True)
    subprocess.run([exe], check=True, timeout=15)
