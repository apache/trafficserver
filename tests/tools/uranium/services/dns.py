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
"""MicroDNS service shared by direct replay and procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from pathlib import Path
import json
import socket

from dnslib import DNSRecord

from ..process import ManagedProcess
from ..runtime import TestRuntime
from ..utils import python_environment_executable
from .process_service import ProcessService


class DNSServer(ProcessService):
    """A microDNS server answering the hostnames a test maps to addresses."""

    def __init__(self, process: ManagedProcess, port: int, zone_file: Path) -> None:
        """Create a microDNS service.

        :param process: Managed microDNS process.
        :param port: UDP listener port.
        :param zone_file: JSON zone document read by microDNS.
        """

        super().__init__(process)
        self._port = port
        self._zone_file = zone_file

    @classmethod
    def create(
        cls,
        runtime: TestRuntime,
        name: str,
        directory: Path,
        *,
        port: int = 0,
        default: str | Sequence[str] | None = None,
        records: Mapping[str, str | Sequence[str]] | None = None,
        test_directory: Path | None = None,
    ) -> DNSServer:
        """Write the zone document and build an unstarted microDNS server.

        :param runtime: Installed tools and the shared port allocator.
        :param name: Process name used for diagnostics and captured output.
        :param directory: Existing directory that receives the zone file and output.
        :param port: UDP listener port, or zero to allocate one.
        :param default: Address or addresses returned for unmapped names, or
            None to let microDNS refuse them.
        :param records: Hostnames mapped to the addresses microDNS returns.
        :param test_directory: Source directory for relative gold-file paths.
        """

        port = port or runtime.allocate_port(socket.SOCK_DGRAM)
        document: dict[str, object] = {"mappings": [_mapping(host, addresses) for host, addresses in (records or {}).items()]}
        if default is not None:
            document["otherwise"] = [default] if isinstance(default, str) else list(default)
        zone_file = directory / "dns_file.json"
        zone_file.write_text(json.dumps(document))
        process = ManagedProcess(
            name,
            [python_environment_executable("microdns"), "INADDR_LOOPBACK", str(port), str(zone_file)],
            directory,
            test_directory=test_directory,
        )
        return cls(process, port, zone_file)

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

    def add_records(self, records: Mapping[str, str | Sequence[str]]) -> None:
        """Append hostname mappings to the zone document.

        :param records: Hostnames mapped to one or more addresses.
        """

        document = json.loads(self._zone_file.read_text())
        document["mappings"].extend(_mapping(hostname, addresses) for hostname, addresses in records.items())
        self._zone_file.write_text(json.dumps(document))


def _mapping(hostname: str, addresses: str | Sequence[str]) -> dict[str, list[str]]:
    """Return one zone entry, rooting @a hostname because microDNS matches fully qualified names.

    :param hostname: Name to resolve, with or without the trailing root dot.
    :param addresses: Address or addresses returned for @a hostname.
    """

    values = [addresses] if isinstance(addresses, str) else list(addresses)
    return {hostname if hostname.endswith(".") else hostname + ".": values}
