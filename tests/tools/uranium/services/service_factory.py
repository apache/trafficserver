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

from collections.abc import Sequence
from pathlib import Path
import re
import shutil
import socket
import subprocess
from typing import Any

import pytest

from ..process import ManagedProcess
from ..utils import python_environment_executable, version_tuple
from .context import ProceduralContext
from .dns import DNSServer
from .httpbin import HttpBinServer
from .origin import OriginServer
from .process_service import ProcessService
from .verifier import VerifierClient, VerifierServer


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
        https_port = int(options.pop("https_port", options.pop("s_port", 0))) or (
            self._context.runtime.allocate_port() if use_ssl or use_both else 0
        )
        ip_value = str(options.pop("ip", "INADDR_LOOPBACK"))
        address = {"INADDR_LOOPBACK": "127.0.0.1", "IN6ADDR_LOOPBACK": "::1"}.get(ip_value, ip_value)
        lookup_key = str(options.pop("lookup_key", "{PATH}"))
        command = [
            python_environment_executable("microserver"),
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
                ]
            )
        for flag, value in options.pop("options", {}).items():
            command.append(str(flag))
            if value:
                command.append(str(value))
        if options:
            raise TypeError(f"Unsupported origin options: {', '.join(sorted(options))}")
        process = ManagedProcess(name, command, directory, test_directory=self._context.test_directory)
        service = OriginServer(process, port, https_port, data_directory, address)
        lookup_headers = re.findall(r"{%([^}]+)}", lookup_key)
        healthcheck_headers = ["GET /ruok HTTP/1.1", "Host: 127.0.0.1"]
        healthcheck_headers.extend(f"{header}: uranium-healthcheck" for header in lookup_headers)
        service.add_response(
            {"headers": "\r\n".join((*healthcheck_headers, "", ""))},
            {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                "body": "imok",
                "options": {"skipHooks": None},
            },
            "healthcheck.json",
        )
        return self._remember(service)

    def dns(self, name: str, **options: Any) -> DNSServer:
        """Create a microDNS server.

        :param name: Unique support-process name within the scenario.
        :param options: ``port`` and ``default`` options for
            :meth:`DNSServer.create`.
        """

        directory = self._directory(name)
        service = DNSServer.create(self._context.runtime, name, directory, test_directory=self._context.test_directory, **options)
        return self._remember(service)

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
        process = ManagedProcess(name, command, directory, test_directory=self._context.test_directory)
        return self._remember(HttpBinServer(process, port))

    def verifier_server(self, name: str, replay_path: str | Path, **options: Any) -> VerifierServer:
        """Create a Proxy Verifier server.

        :param name: Unique support-process name within the scenario.
        :param replay_path: Replay file served by verifier-server.
        :param options: Options for :meth:`VerifierServer.create`. One HTTP and
            one HTTPS listener port are allocated unless ``http_ports`` or
            ``https_ports`` is given.
        """

        directory = self._directory(name)
        runtime = self._context.runtime
        options.setdefault("http_ports", [runtime.allocate_port()])
        options.setdefault("https_ports", [runtime.allocate_port()])
        service = VerifierServer.create(
            runtime,
            name,
            directory,
            self._context.resolve_path(replay_path),
            test_directory=self._context.test_directory,
            **options,
        )
        return self._remember(service)

    def verifier_client(self, name: str, replay_path: str | Path, **options: Any) -> VerifierClient:
        """Create a Proxy Verifier client.

        :param name: Unique support-process name within the scenario.
        :param replay_path: Replay file sent by verifier-client.
        :param options: Options for :meth:`VerifierClient.create`, where
            ``return_code`` may be one exit status or a sequence of them.
        """

        directory = self._directory(name)
        return_code = options.pop("return_code", 0)
        is_sequence = isinstance(return_code, Sequence) and not isinstance(return_code, str)
        service = VerifierClient.create(
            self._context.runtime,
            name,
            directory,
            self._context.resolve_path(replay_path),
            return_codes=return_code if is_sequence else [return_code],
            test_directory=self._context.test_directory,
            **options,
        )
        return self._remember(service)

    def process(
        self,
        name: str,
        command: Sequence[str | Path],
        *,
        environment: dict[str, str] | None = None,
        ready_port: int = 0,
        ready_address: str = "127.0.0.1",
    ) -> ProcessService:
        """Create an arbitrary managed process for a bespoke scenario.

        :param name: Unique support-process name within the scenario.
        :param command: Executable followed by its command-line arguments.
        :param environment: Complete or augmented process environment.
        :param ready_port: TCP port that must accept connections after start,
            or zero to skip listener readiness checks.
        :param ready_address: Address used for listener readiness checks.
        """

        directory = self._directory(name)
        process = ManagedProcess(
            name,
            command,
            directory,
            environment,
            test_directory=self._context.test_directory,
        )
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
