#!/usr/bin/env python3
'''
Shut Traffic Server down while a ts_lua state is executing a request callback.
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import argparse
import signal
import socket
import sys
import threading
import time
from pathlib import Path

# Copied into the run directory alongside this script.
import ts_process_handler

# Concurrent /hold requests. global_shutdown.lua holds one Lua state per request,
# so several in flight keep that state busy essentially all of the time, whichever
# event thread the shutdown continuation happens to land on.
LOAD_THREADS = 4

# The same idea for the remap instance of the plugin: keep remap Lua states busy,
# and requests queued on their mutexes, across the shutdown.
REMAP_LOAD_THREADS = 4

# How long the load keeps running after SIGTERM. It has to outlast the delay
# between the signal and TS_LIFECYCLE_SHUTDOWN_HOOK (SignalContinuation polls
# every 500ms), and it has to end well inside the plugin's barrier timeout so the
# __shutdown__ callbacks still run.
LOAD_AFTER_SIGNAL_SECONDS = 1.5

# How long to wait for the Lua states to start reporting themselves busy.
STATES_ACTIVE_TIMEOUT_SECONDS = 15


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', help='The Traffic Server host to send requests to.')
    parser.add_argument('port', type=int, help='The Traffic Server port to send requests to.')
    parser.add_argument('test_directory', type=Path, help='The directory the Lua script writes its state markers in.')
    parser.add_argument('ts_identifier', help='An identifier in the command line of the Traffic Server process to signal.')
    return parser.parse_args()


def send_requests(host: str, port: int, stop: threading.Event, request: bytes) -> None:
    """Send one request repeatedly until stop is set."""
    while not stop.is_set():
        try:
            with socket.create_connection((host, port), timeout=10) as connection:
                connection.settimeout(10)
                connection.sendall(request)
                while connection.recv(4096):
                    pass
        except OSError:
            # A Lua state held by another request stalls its event thread, so a
            # connection can fail while the load is meant to keep running. Only
            # stop asked for; giving up here would let the load die before the
            # shutdown it is supposed to span.
            if stop.is_set():
                return
            time.sleep(0.05)


# The global script holds a Lua state for /hold; the remap script holds one of the
# remap states for /remap-hold.
GLOBAL_REQUEST = b'GET /hold HTTP/1.1\r\nHost: www.example.com\r\nConnection: close\r\n\r\n'
REMAP_REQUEST = b'GET /remap-hold HTTP/1.1\r\nHost: remap.example.com\r\nConnection: close\r\n\r\n'


def wait_for_active_state(marker: Path) -> bool:
    """Wait until the Lua script reports a state busy in a request callback."""
    deadline = time.monotonic() + STATES_ACTIVE_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if marker.exists():
            return True
        time.sleep(0.01)
    return False


def main() -> int:
    args = parse_args()
    stop = threading.Event()
    loaders = [
        threading.Thread(target=send_requests, args=(args.host, args.port, stop, GLOBAL_REQUEST), daemon=True)
        for _ in range(LOAD_THREADS)
    ]
    loaders += [
        threading.Thread(target=send_requests, args=(args.host, args.port, stop, REMAP_REQUEST), daemon=True)
        for _ in range(REMAP_LOAD_THREADS)
    ]

    for loader in loaders:
        loader.start()

    # global_shutdown.lua holds Lua state 1; state 0 is left idle so that its
    # __shutdown__ callback runs while state 1 is still executing Lua.
    # remap_shutdown.lua holds the single remap state, with the remaining remap
    # requests waiting on its mutex.
    for marker in ('lua-state-1.active', 'lua-remap-state.active'):
        if not wait_for_active_state(args.test_directory / marker):
            print(f'{marker} never appeared', file=sys.stderr)
            return 1

    try:
        process = ts_process_handler.get_ts_process_pid(args.ts_identifier)
    except ts_process_handler.GetPidError as e:
        print(e, file=sys.stderr)
        return 1

    process.send_signal(signal.SIGTERM)

    # Keep the states busy across the shutdown hook, then let them quiesce.
    time.sleep(LOAD_AFTER_SIGNAL_SECONDS)
    stop.set()

    for loader in loaders:
        loader.join(timeout=15)

    return 0


if __name__ == '__main__':
    sys.exit(main())
