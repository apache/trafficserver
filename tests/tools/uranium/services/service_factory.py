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
"""Factory for non-ATS services used by procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from pathlib import Path
import json
import re
import shlex
import shutil
import socket
import subprocess
from typing import Any

import pytest

from ..process import ManagedProcess
from ..utils import loopback_addresses, version_tuple
from .context import ProceduralContext
from .dns import DNSServer
from .httpbin import HttpBinServer
from .origin import OriginServer
from .process_service import ProcessService
from .verifier import VerifierServer


class ServiceFactory:
    """Create pytest-owned support processes for procedural tests."""

    def __init__(self, context: ProceduralContext) -> None:
        """Create a support-service factory for one scenario.

        :param context: Runtime and sandbox state for the pytest item.
        """

        self._context = context
        self._services: list[ProcessService] = []
        self._names: set[str] = set()

    def _directory(self, name: str) -> Path:
        """Create a uniquely named support-process directory.

        :param name: Unique support-process name within the scenario.
        """

        if name in self._names:
            raise ValueError(f"Support process {name!r} already exists")
        self._names.add(name)
        directory = self._context.run_directory / name
        directory.mkdir(parents=True)
        return directory

    def allocate_port(self, socket_type: int = socket.SOCK_STREAM) -> int:
        """Reserve a listener port for a bespoke support process.

        :param socket_type: Socket type, normally ``SOCK_STREAM`` or
            ``SOCK_DGRAM``.
        """

        return self._context.runtime.allocate_port(socket_type)

    def resolve_path(self, value: str | Path) -> Path:
        """Resolve a source path, placeholder, or generated test artifact.

        :param value: Absolute path or path relative to the test directory.
        """

        return self._context.resolve_path(value)

    def origin(self, name: str, **options: Any) -> OriginServer:
        """Create a microserver origin.

        :param name: Unique support-process name within the scenario.
        :param options: Microserver listener, TLS, delay, and command options.
        """

        directory = self._directory(name)
        data_directory = directory / "data"
        data_directory.mkdir()
        use_ssl = bool(options.pop("ssl", False))
        use_both = bool(options.pop("both", False))
        port = int(options.pop("port", 0)) or (self._context.runtime.allocate_port() if not use_ssl or use_both else 0)
        https_port = int(options.pop("https_port", options.pop(
            "s_port", 0))) or (self._context.runtime.allocate_port() if use_ssl or use_both else 0)
        ip_value = str(options.pop("ip", "INADDR_LOOPBACK"))
        address = {"INADDR_LOOPBACK": "127.0.0.1", "IN6ADDR_LOOPBACK": "::1"}.get(ip_value, ip_value)
        lookup_key = str(options.pop("lookup_key", "{PATH}"))
        command = [
            "microserver",
            "--data-dir",
            data_directory,
            "--ip_address",
            address,
            "--lookupkey",
            lookup_key,
        ]
        delay = options.pop("delay", None)
        if delay:
            command.extend(["--delay", str(delay)])
        if port:
            command.extend(["--port", port])
        if use_ssl or use_both:
            ssl_dir = self._context.runtime.test_tools / "microserver" / "ssl"
            command.append("--both" if use_both else "--ssl")
            command.extend(
                [
                    "--key",
                    options.pop("clientkey", ssl_dir / "server.pem"),
                    "--cert",
                    options.pop("clientcert", ssl_dir / "server.crt"),
                    "--s_port",
                    https_port,
                ])
        for flag, value in options.pop("options", {}).items():
            command.append(str(flag))
            if value:
                command.append(str(value))
        if options:
            raise TypeError(f"Unsupported origin options: {', '.join(sorted(options))}")
        process = ManagedProcess(name, command, directory)
        service = OriginServer(process, port, https_port, data_directory, address)
        lookup_headers = re.findall(r"{%([^}]+)}", lookup_key)
        healthcheck_headers = ["GET /ruok HTTP/1.1", "Host: 127.0.0.1"]
        healthcheck_headers.extend(f"{header}: uranium-healthcheck" for header in lookup_headers)
        service.add_response(
            {"headers": "\r\n".join((*healthcheck_headers, "", ""))},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "imok",
                "options": {
                    "skipHooks": None
                },
            },
            "healthcheck.json",
        )
        return self._remember(service)

    def dns(self, name: str, **options: Any) -> DNSServer:
        """Create a microDNS server.

        :param name: Unique support-process name within the scenario.
        :param options: Listener port and default DNS response options.
        """

        directory = self._directory(name)
        port = int(options.pop("port", 0))
        while not port:
            candidate = self._context.runtime.allocate_port()
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                try:
                    probe.bind(("127.0.0.1", candidate))
                except OSError:
                    continue
            port = candidate
        default = options.pop("default", None)
        otherwise = [default] if isinstance(default, str) else default
        zone_file = directory / "dns_file.json"
        zone_file.write_text(json.dumps({"mappings": [], **({"otherwise": otherwise} if otherwise else {})}))
        process = ManagedProcess(name, ["microdns", "INADDR_LOOPBACK", port, zone_file], directory)
        return self._remember(DNSServer(process, port, zone_file))

    def httpbin(self, name: str, **options: Any) -> HttpBinServer:
        """Create a go-httpbin server.

        :param name: Unique support-process name within the scenario.
        :param options: Listener address, port, and command options.
        """

        if shutil.which("go-httpbin") is None:
            pytest.skip("go-httpbin is required")
        directory = self._directory(name)
        port = int(options.pop("port", 0)) or self._context.runtime.allocate_port()
        command = ["go-httpbin", "-host", options.pop("ip", "127.0.0.1"), "-port", port]
        for flag, value in options.pop("options", {}).items():
            command.extend([str(flag), str(value)])
        process = ManagedProcess(name, command, directory)
        return self._remember(HttpBinServer(process, port))

    def verifier_server(self, name: str, replay_path: str | Path, **options: Any) -> VerifierServer:
        """Create a Proxy Verifier server.

        :param name: Unique support-process name within the scenario.
        :param replay_path: Replay file served by verifier-server.
        :param options: Listener, TLS, verbosity, and command options.
        """

        directory = self._directory(name)
        http_ports = list(options.pop("http_ports", [self._context.runtime.allocate_port()]))
        https_ports = list(options.pop("https_ports", [self._context.runtime.allocate_port()]))
        ssl_dir = self._context.runtime.test_tools / "proxy-verifier" / "ssl"
        command: list[str | Path | int] = [self._context.runtime.verifier_bin / "verifier-server", "run"]
        if http_ports:
            command.extend(["--listen-http", loopback_addresses(http_ports)])
        if https_ports:
            command.extend(["--listen-https", loopback_addresses(https_ports)])
            command.extend(
                [
                    "--server-cert",
                    options.pop("ssl_cert", ssl_dir / "server.pem"),
                    "--ca-certs",
                    options.pop("ca_cert", ssl_dir / "ca.pem"),
                ])
        command.append(self._context.resolve_path(replay_path))
        if options.pop("verbose", True):
            command.extend(["--verbose", "diag"])
        command.extend(shlex.split(str(options.pop("other_args", ""))))
        process = ManagedProcess(name, command, directory)
        return self._remember(VerifierServer(process, http_ports[0] if http_ports else 0, https_ports[0] if https_ports else 0))

    def verifier_client(self, name: str, replay_path: str | Path, **options: Any) -> ProcessService:
        """Create a Proxy Verifier client.

        :param name: Unique support-process name within the scenario.
        :param replay_path: Replay file sent by verifier-client.
        :param options: Target ports, TLS, keys, and command options.
        """

        directory = self._directory(name)
        http_ports = list(options.pop("http_ports", []))
        https_ports = list(options.pop("https_ports", []))
        http3_ports = list(options.pop("http3_ports", []))
        command: list[str | Path] = [
            self._context.runtime.verifier_bin / "verifier-client",
            "run",
            self._context.resolve_path(replay_path),
        ]
        if http_ports:
            command.extend(["--connect-http", loopback_addresses(http_ports)])
        if https_ports:
            command.extend(["--connect-https", loopback_addresses(https_ports)])
        if http3_ports:
            command.extend(["--connect-http3", loopback_addresses(http3_ports)])
        if https_ports or http3_ports:
            ssl_dir = self._context.runtime.test_tools / "proxy-verifier" / "ssl"
            command.extend(
                [
                    "--client-cert",
                    options.pop("ssl_cert", ssl_dir / "client.pem"),
                    "--ca-certs",
                    options.pop("ca_cert", ssl_dir / "ca.pem"),
                ])
        keys = options.pop("keys", None)
        if keys:
            command.append("--keys")
            command.extend(shlex.split(keys) if isinstance(keys, str) else [str(key) for key in keys])
        if options.pop("verbose", True):
            command.extend(["--verbose", "diag"])
        poll_timeout = options.pop("poll_timeout", None)
        if poll_timeout is not None:
            command.extend(["--poll-timeout", str(poll_timeout)])
        other_args = str(options.pop("other_args", ""))
        command.extend(shlex.split(other_args))
        if "thread-limit" not in other_args:
            command.extend(["--thread-limit", "1"])
        return_code = options.pop("return_code", 0)
        return_codes = return_code if isinstance(return_code, Sequence) and not isinstance(return_code, str) else [return_code]
        process = ManagedProcess(name, command, directory, expected_return_codes=return_codes)
        reject = "" if options.pop("allow_errors", False) else "Violation|Invalid status"
        return self._remember(ProcessService(process, reject_expression=reject))

    def process(
        self,
        name: str,
        command: Sequence[str | Path],
        *,
        expected_return_codes: Iterable[int] = (0,),
        environment: dict[str, str] | None = None,
        ready_port: int = 0,
        ready_address: str = "127.0.0.1",
    ) -> ProcessService:
        """Create an arbitrary managed process for a bespoke scenario.

        :param name: Unique support-process name within the scenario.
        :param command: Executable followed by its command-line arguments.
        :param expected_return_codes: Process return codes treated as success.
        :param environment: Complete or augmented process environment.
        :param ready_port: TCP port that must accept connections after start,
            or zero to skip listener readiness checks.
        :param ready_address: Address used for listener readiness checks.
        """

        directory = self._directory(name)
        process = ManagedProcess(name, command, directory, environment, expected_return_codes)
        return self._remember(ProcessService(process, ready_port=ready_port, ready_address=ready_address))

    def proxy_verifier_at_least(self, version: str) -> bool:
        """Return whether Proxy Verifier meets a minimum version.

        :param version: Minimum dotted version string.
        """

        binary = self._context.runtime.verifier_bin / "verifier-client"
        result = subprocess.run((str(binary), "--version"), capture_output=True, text=True, check=False)
        found = re.search(r"\d+(?:\.\d+)+", result.stdout + result.stderr)
        return found is not None and version_tuple(found.group()) >= version_tuple(version)

    def close(self) -> None:
        failures = []
        for service in reversed(self._services):
            try:
                service.close()
            except Exception as error:
                failures.append(error)
        self._services.clear()
        self._names.clear()
        if failures:
            raise ExceptionGroup("Support service cleanup failed", failures)

    def _remember(self, service: Any) -> Any:
        """Register a support service for reverse-order cleanup.

        :param service: Fixture-owned support service to register and return.
        """

        self._services.append(service)
        return service
