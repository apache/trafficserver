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
"""MicroDNS service for procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from pathlib import Path
import json
import socket

from dnslib import DNSRecord

from ..process import ManagedProcess
from .process_service import ProcessService


class DNSServer(ProcessService):
    """A pytest-owned microDNS server."""

    def __init__(self, process: ManagedProcess, port: int, zone_file: Path) -> None:
        """Create a microDNS service.

        :param process: Managed microDNS process.
        :param port: UDP listener port.
        :param zone_file: JSON zone document read by microDNS.
        """

        super().__init__(process)
        self._port = port
        self._zone_file = zone_file

    @property
    def port(self) -> int:
        return self._port

    def start(self) -> None:
        super().start()

        def responds() -> bool:
            try:
                DNSRecord.question("pytest-readiness.invalid").send("127.0.0.1", self.port, timeout=0.1)
                return True
            except (OSError, socket.timeout):
                return False

        self._process.wait_until(responds, 10, f"DNS replies on 127.0.0.1:{self.port}")

    def add_records(self, records: Mapping[str, Sequence[str]]) -> None:
        """Append hostname mappings to the zone document.

        :param records: Hostnames mapped to one or more addresses.
        """

        document = json.loads(self._zone_file.read_text())
        for hostname, addresses in records.items():
            document["mappings"].append({hostname if hostname.endswith(".") else hostname + ".": list(addresses)})
        self._zone_file.write_text(json.dumps(document))
