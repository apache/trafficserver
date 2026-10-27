"""Request explicit origin shutdown and wait for its successful completion."""
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

import os
from pathlib import Path
import sys
import ssl
import time

import psutil


def send_bytes(tls_socket: ssl.SSLSocket, payload: bytes) -> None:
    """Allow writes time to finish independently of the short read poll."""
    read_timeout = tls_socket.gettimeout()
    try:
        tls_socket.settimeout(5)
        tls_socket.sendall(payload)
    finally:
        tls_socket.settimeout(read_timeout)


class OriginLifecycle:
    """Keep an origin alive until the test finishes its client and counter checks."""

    def __init__(self, stop_file: str) -> None:
        self.stop_file = Path(stop_file)
        self.done_file = Path(f"{stop_file}.done")

    @property
    def stopped(self) -> bool:
        return self.stop_file.exists()

    def complete(self) -> None:
        """Publish completion only after all origin-side assertions succeed."""
        pending = Path(f"{self.done_file}.tmp")
        pending.write_text(str(os.getpid()))
        pending.replace(self.done_file)


def stop_origin(stop_file: str) -> None:
    lifecycle = OriginLifecycle(stop_file)
    lifecycle.stop_file.touch()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if lifecycle.done_file.exists():
            pid = int(lifecycle.done_file.read_text())
            try:
                # AuTest may defer reaping until this shutdown run finishes.
                if psutil.Process(pid).status() in (psutil.STATUS_ZOMBIE, psutil.STATUS_DEAD):
                    return
            except psutil.NoSuchProcess:
                return
            except psutil.AccessDenied as error:
                raise RuntimeError(f"Cannot verify origin PID {pid} exited after validation") from error
        time.sleep(0.05)
    raise RuntimeError("Origin did not complete validation and exit after the stop request")


if __name__ == "__main__":
    stop_origin(sys.argv[1])
