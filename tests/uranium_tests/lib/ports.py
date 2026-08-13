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
"""Port helpers backed by the pytest Uranium runtime allocator."""

from __future__ import annotations

from contextlib import contextmanager
from typing import Iterator
import socket

from uranium_testkit.scenario import _scenario


def PortOpen(port: int, address: str | None = None, bound_ports: set[int] | None = None) -> bool:  # noqa: N802
    """Return whether @a port is already bound or accepts a connection."""

    if bound_ports and port in bound_ports:
        return True
    try:
        with socket.create_connection((address or "localhost", port), timeout=0.2):
            return True
    except (OSError, socket.timeout):
        return False


@contextmanager
def get_port_number() -> Iterator[int]:
    """Yield a unique port allocated for the active pytest scenario."""

    yield _scenario().runtime.allocate_port()


def get_port(obj: object, name: str) -> int:
    """Allocate a port and assign it to ``obj.Variables[name]``."""

    port = _scenario().runtime.allocate_port()
    obj.Variables[name] = port
    return port
