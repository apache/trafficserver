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
"""Small utilities shared by direct and procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Iterable
import re
import socket


def loopback_addresses(ports: Iterable[int]) -> str:
    """Format loopback listener addresses for Proxy Verifier.

    :param ports: TCP port numbers to format.
    """

    return ",".join(f"127.0.0.1:{port}" for port in ports)


def tcp_open(port: int, address: str = "127.0.0.1") -> bool:
    """Return whether a TCP listener accepts a short probe connection.

    :param port: TCP port number to probe.
    :param address: IP address or hostname to probe.
    """

    try:
        with socket.create_connection((address, port), timeout=0.1):
            return True
    except OSError:
        return False


def version_tuple(value: str) -> tuple[int, ...]:
    """Convert a dotted version string to comparable integer components.

    :param value: Version string containing numeric components.
    """

    match = re.search(r"\d+(?:\.\d+)+", value)
    return tuple(int(part) for part in match.group(0).split(".")) if match else ()
