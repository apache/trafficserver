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
"""Assertion and polling helpers for procedural Uranium tests."""

from __future__ import annotations

from pathlib import Path
import re
import socket
import time
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .ats import ATS


def wait_for_file_lines(path: Path, expression: str, count: int, timeout: float = 10) -> str:
    """Wait until a file contains enough matching lines.

    :param path: File path to poll.
    :param expression: Regular expression matched against the file contents.
    :param count: Minimum number of required matches.
    :param timeout: Maximum number of seconds to wait.
    """

    deadline = time.monotonic() + timeout
    content = ""
    while time.monotonic() < deadline:
        if path.exists():
            content = path.read_text(errors="replace")
            if len(re.findall(expression, content, re.MULTILINE)) >= count:
                return content
        time.sleep(0.1)
    raise AssertionError(f"Expected {count} matches for {expression!r} in {path}.\n{content}")


def send_tcp(port: int, data: str | bytes, *, address: str = "127.0.0.1", timeout: float = 10) -> str:
    """Send bytes to a TCP listener and return its response.

    :param port: Destination TCP port.
    :param data: Text or bytes to send before closing the write side.
    :param address: Destination IP address or hostname.
    :param timeout: Connection and receive timeout in seconds.
    """

    payload = data.encode() if isinstance(data, str) else data
    chunks = []
    with socket.create_connection((address, port), timeout=timeout) as connection:
        connection.settimeout(timeout)
        connection.sendall(payload)
        connection.shutdown(socket.SHUT_WR)
        while chunk := connection.recv(65536):
            chunks.append(chunk)
    return b"".join(chunks).decode(errors="replace")


def wait_for_metric(ats: ATS, name: str, expected: int, timeout: float = 10) -> int:
    """Wait until an ATS metric reaches an expected value.

    :param ats: Traffic Server instance queried through traffic_ctl.
    :param name: Fully qualified metric name.
    :param expected: Metric value required for success.
    :param timeout: Maximum number of seconds to wait.
    """

    deadline = time.monotonic() + timeout
    value = 0
    output = ""
    while time.monotonic() < deadline:
        result = ats.traffic_ctl("metric", "get", name)
        output = result.output
        if result.returncode == 0:
            value = int(result.stdout.split()[-1])
            if value == expected:
                return value
        time.sleep(0.1)
    raise AssertionError(f"Expected metric {name} to reach {expected}, found {value}.\n{output}")
