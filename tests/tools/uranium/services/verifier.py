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
"""Proxy Verifier services shared by direct replay and procedural Uranium tests."""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from pathlib import Path
import shlex

from ..process import ManagedProcess
from ..runtime import TestRuntime
from ..utils import loopback_addresses, tcp_open
from .process_service import ProcessService

SERVER_REJECT_EXPRESSION = "Violation"
"""Server output that means a request did not match the replay's expectations."""

CLIENT_REJECT_EXPRESSION = "Violation|Invalid status"
"""Client output that means a proxied response did not match the replay's expectations."""


class VerifierServer(ProcessService):
    """A Proxy Verifier server playing the origin for replayed transactions."""

    def __init__(self, process: ManagedProcess, http_ports: Sequence[int], https_ports: Sequence[int]) -> None:
        """Create a Proxy Verifier server service.

        :param process: Managed verifier-server process.
        :param http_ports: Clear-text HTTP listener ports.
        :param https_ports: HTTPS listener ports.
        """

        super().__init__(process, reject_expression=SERVER_REJECT_EXPRESSION)
        self._http_ports = tuple(http_ports)
        self._https_ports = tuple(https_ports)

    @classmethod
    def create(
        cls,
        runtime: TestRuntime,
        name: str,
        directory: Path,
        replay_path: Path,
        *,
        http_ports: Sequence[int],
        https_ports: Sequence[int],
        ssl_cert: Path | None = None,
        ca_cert: Path | None = None,
        verbose: bool = True,
        other_args: str = "",
        return_codes: Iterable[int] = (0,),
        test_directory: Path | None = None,
    ) -> VerifierServer:
        """Build an unstarted verifier-server that serves @a replay_path.

        :param runtime: Installed tools, including the Proxy Verifier binaries.
        :param name: Process name used for diagnostics and captured output.
        :param directory: Existing directory that receives the server's output.
        :param replay_path: Replay file or directory whose server responses are played.
        :param http_ports: Clear-text HTTP listener ports; may be empty.
        :param https_ports: HTTPS listener ports; may be empty.
        :param ssl_cert: Server certificate, or None for the shared test certificate.
        :param ca_cert: CA bundle, or None for the shared test CA.
        :param verbose: Whether to log diagnostics needed to debug a failure.
        :param other_args: Extra shell-quoted verifier-server arguments.
        :param return_codes: Exit statuses treated as success.
        :param test_directory: Source directory for relative gold-file paths.
        """

        if not http_ports and not https_ports:
            raise ValueError(f"{name} needs at least one listener port")
        ssl_dir = runtime.test_tools / "proxy-verifier" / "ssl"
        command: list[str | Path] = [runtime.verifier_bin / "verifier-server", "run"]
        if http_ports:
            command.extend(["--listen-http", loopback_addresses(http_ports)])
        if https_ports:
            command.extend(
                [
                    "--listen-https",
                    loopback_addresses(https_ports),
                    "--server-cert",
                    ssl_cert or ssl_dir / "server.pem",
                    "--ca-certs",
                    ca_cert or ssl_dir / "ca.pem",
                    "--tls-secrets-log-file",
                    directory / "tls_secrets.txt",
                ]
            )
        command.append(replay_path)
        if verbose:
            command.extend(["--verbose", "diag"])
        command.extend(shlex.split(other_args))
        process = ManagedProcess(name, command, directory, expected_return_codes=return_codes, test_directory=test_directory)
        return cls(process, http_ports, https_ports)

    @property
    def http_port(self) -> int:
        """Return the first clear-text HTTP listener port, or zero when there is none."""

        return self._http_ports[0] if self._http_ports else 0

    @property
    def https_port(self) -> int:
        """Return the first HTTPS listener port, or zero when there is none."""

        return self._https_ports[0] if self._https_ports else 0

    def start(self) -> None:
        super().start()
        # Probe TLS listeners only when there is no clear-text one, because a
        # bare TCP probe of a TLS port makes verifier-server log a failed handshake.
        ports = self._http_ports or self._https_ports[:1]
        self._process.wait_until(
            lambda: all(tcp_open(port) for port in ports),
            10,
            f"Proxy Verifier listeners on {loopback_addresses(ports)}",
        )


class VerifierClient(ProcessService):
    """A Proxy Verifier client that replays transactions through Traffic Server."""

    @classmethod
    def create(
        cls,
        runtime: TestRuntime,
        name: str,
        directory: Path,
        replay_path: Path,
        *,
        http_ports: Sequence[int] = (),
        https_ports: Sequence[int] = (),
        http3_ports: Sequence[int] = (),
        ssl_cert: Path | None = None,
        ca_cert: Path | None = None,
        keys: str | Sequence[object] | None = None,
        verbose: bool = True,
        poll_timeout: object | None = None,
        other_args: str = "",
        run_parallel: bool = False,
        allow_errors: bool = False,
        return_codes: Iterable[int] = (0,),
        test_directory: Path | None = None,
    ) -> VerifierClient:
        """Build an unstarted verifier-client that sends @a replay_path.

        :param runtime: Installed tools, including the Proxy Verifier binaries.
        :param name: Process name used for diagnostics and captured output.
        :param directory: Existing directory that receives the client's output.
        :param replay_path: Replay file or directory whose client requests are sent.
        :param http_ports: Proxy ports receiving clear-text HTTP.
        :param https_ports: Proxy ports receiving HTTP over TLS.
        :param http3_ports: Proxy ports receiving HTTP/3.
        :param ssl_cert: Client certificate, or None for the shared test certificate.
        :param ca_cert: CA bundle, or None for the shared test CA.
        :param keys: Transaction keys to replay, as a shell-quoted string or a
            sequence, or None to replay every transaction.
        :param verbose: Whether to log diagnostics needed to debug a failure.
        :param poll_timeout: Value for ``--poll-timeout``, or None for the default.
        :param other_args: Extra shell-quoted verifier-client arguments.
        :param run_parallel: Whether sessions may run concurrently; otherwise the
            client uses one thread so transactions keep replay order.
        :param allow_errors: Whether verification failures in the output are expected.
        :param return_codes: Exit statuses treated as success.
        :param test_directory: Source directory for relative gold-file paths.
        """

        command: list[str | Path] = [runtime.verifier_bin / "verifier-client", "run", replay_path]
        if http_ports:
            command.extend(["--connect-http", loopback_addresses(http_ports)])
        if https_ports:
            command.extend(["--connect-https", loopback_addresses(https_ports)])
        if http3_ports:
            qlog = directory / "qlog_directory"
            qlog.mkdir(exist_ok=True)
            command.extend(["--connect-http3", loopback_addresses(http3_ports), "--qlog-dir", qlog])
        if https_ports or http3_ports:
            ssl_dir = runtime.test_tools / "proxy-verifier" / "ssl"
            command.extend(
                [
                    "--client-cert",
                    ssl_cert or ssl_dir / "client.pem",
                    "--ca-certs",
                    ca_cert or ssl_dir / "ca.pem",
                    "--tls-secrets-log-file",
                    directory / "tls_secrets.txt",
                ]
            )
        if verbose:
            command.extend(["--verbose", "diag"])
        if poll_timeout is not None:
            command.extend(["--poll-timeout", str(poll_timeout)])
        command.extend(shlex.split(other_args))
        if "thread-limit" not in other_args and not run_parallel:
            command.extend(["--thread-limit", "1"])
        # verifier-client reads every argument after --keys as another key,
        # so the keys must come last.
        if keys:
            command.append("--keys")
            command.extend(shlex.split(keys) if isinstance(keys, str) else [str(key) for key in keys])
        process = ManagedProcess(name, command, directory, expected_return_codes=return_codes, test_directory=test_directory)
        return cls(process, reject_expression="" if allow_errors else CLIENT_REJECT_EXPRESSION)
