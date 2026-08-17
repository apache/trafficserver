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
"""Native pytest service objects for procedural Uranium scenarios."""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
import difflib
import json
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import time
from typing import Any

from dnslib import DNSRecord
import pytest

from .config import ReplaySpec
from .process import ManagedProcess
from .replay import ReplayTest
from .runtime import TestRuntime


@dataclass(frozen=True)
class CommandResult:
    """The observable result of a client command."""

    command: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str

    @property
    def output(self) -> str:
        """Return stdout and stderr together for assertion diagnostics."""

        return self.stdout + self.stderr


@dataclass(frozen=True)
class ProceduralContext:
    """Runtime and sandbox state shared by one procedural pytest item."""

    runtime: TestRuntime
    node_name: str
    test_path: Path
    run_directory: Path
    use_uds: bool = False

    @classmethod
    def create(
        cls,
        runtime: TestRuntime,
        node_name: str,
        test_path: Path,
        *,
        use_uds: bool = False,
    ) -> "ProceduralContext":
        """Create and empty the sandbox for one pytest test."""

        run_directory = runtime.procedural_sandbox(node_name)
        runtime.prepare_sandbox(run_directory)
        return cls(runtime, node_name, test_path, run_directory, use_uds)

    @property
    def test_directory(self) -> Path:
        """Return the source directory containing the procedural test."""

        return self.test_path.parent

    def resolve_path(self, value: str | Path) -> Path:
        """Resolve a source or build artifact used by this test."""

        path = Path(value)
        if path.is_absolute():
            return path
        source = self.test_directory / path
        return source if source.exists() else self.runtime.resolve_artifact(self.test_directory, str(value))


class RecordsConfig:
    """Stage records.yaml updates until ATS starts."""

    def __init__(self, values: dict[str, object]) -> None:
        self._values = values

    def update(self, values: Mapping[str, object]) -> None:
        """Merge flat ATS record names into the staged values."""

        self._values.update(values)


class ConfigFile:
    """Stage one line-oriented ATS configuration file."""

    def __init__(self, lines: list[str], path: Path) -> None:
        self._lines = lines
        self._path = path

    @property
    def path(self) -> Path:
        """Return the configuration file's materialized path."""

        return self._path

    def add_line(self, line: object) -> None:
        """Append one line to the staged file."""

        self._lines.append(str(line))

    def add_lines(self, lines: Sequence[object] | str) -> None:
        """Append lines from a string or sequence."""

        values = lines.splitlines() if isinstance(lines, str) else lines
        self._lines.extend(str(line) for line in values)


class ATS:
    """A pytest-owned Traffic Server process with staged configuration."""

    _CONFIG_FILENAMES = {
        "cache_config": "cache.config",
        "hosting_config": "hosting.config",
        "ip_allow_lines": "ip_allow.yaml",
        "parent_config": "parent.config",
        "plugin_config": "plugin.config",
        "remap_config": "remap.config",
        "remap_yaml_lines": "remap.yaml",
        "splitdns_config": "splitdns.config",
        "ssl_multicert_lines": "ssl_multicert.yaml",
        "storage_lines": "storage.yaml",
    }

    def __init__(self, context: ProceduralContext, name: str = "ats", **process_options: Any) -> None:
        self._context = context
        self._process_options = dict(process_options)
        return_code = self._process_options.pop("return_code", None)
        self._records: dict[str, object] = {}
        self._config_lines = {key: [] for key in self._CONFIG_FILENAMES}
        self._config: dict[str, Any] = {}
        if return_code is not None:
            self._config["return_code"] = return_code
        spec = ReplaySpec(
            path=context.test_path,
            document={},
            urtest={
                "server": {},
                "client": {},
                "ats": {}
            },
        )
        self._runner = ReplayTest(spec, context.runtime, f"{context.node_name}-{name}")
        self._runner.sandbox = context.run_directory
        self._name = name
        self._root = context.run_directory / name
        self._ipv6_port = context.runtime.allocate_port()
        self._ipv6_https_port = context.runtime.allocate_port()
        self._process: ManagedProcess | None = None
        self._was_started = False
        self._allow_fatal_diagnostics = False
        self.records = RecordsConfig(self._records)

    @property
    def name(self) -> str:
        """Return this Traffic Server instance's process name."""

        return self._name

    @property
    def http_port(self) -> int:
        """Return the selected IPv4 HTTP port."""

        return self._runner.http_port

    @property
    def https_port(self) -> int:
        """Return the selected IPv4 TLS port."""

        return self._runner.https_port

    @property
    def ipv6_port(self) -> int:
        """Return a port reserved for an explicit IPv6 listener."""

        return self._ipv6_port

    @property
    def ipv6_https_port(self) -> int:
        """Return a port reserved for an explicit IPv6 TLS listener."""

        return self._ipv6_https_port

    @property
    def proxy_protocol_port(self) -> int:
        """Return the selected clear-text Proxy Protocol port."""

        return self._runner.proxy_protocol_port

    @property
    def proxy_protocol_https_port(self) -> int:
        """Return the selected TLS Proxy Protocol port."""

        return self._runner.proxy_protocol_https_port

    @property
    def uds_path(self) -> str:
        """Return this instance's Unix-domain listener path."""

        return str(self._root / "runtime" / "ats.sock")

    @property
    def run_directory(self) -> Path:
        """Return this process's isolated run directory."""

        return self._root

    @property
    def config_directory(self) -> Path:
        """Return the ATS configuration directory."""

        return self._root / "config"

    @property
    def log_directory(self) -> Path:
        """Return the ATS log directory."""

        return self._root / "log"

    @property
    def storage_directory(self) -> Path:
        """Return the ATS storage directory."""

        return self._root / "storage"

    @property
    def runtime_directory(self) -> Path:
        """Return the ATS local-state directory."""

        return self._root / "runtime"

    @property
    def ssl_directory(self) -> Path:
        """Return the ATS TLS material directory."""

        return self._root / "ssl"

    @property
    def diags_log(self) -> Path:
        """Return the path to diags.log."""

        return self.log_directory / "diags.log"

    @property
    def traffic_out(self) -> Path:
        """Return the path to traffic.out."""

        return self.log_directory / "traffic.out"

    @property
    def error_log(self) -> Path:
        """Return the path to error.log."""

        return self.log_directory / "error.log"

    @property
    def environment(self) -> dict[str, str]:
        """Return the environment used by ATS and its tools."""

        if self._runner.ats_environment is None:
            raise RuntimeError(f"{self.name} has not been started")
        return self._runner.ats_environment

    def _line_config(self, name: str) -> ConfigFile:
        return ConfigFile(self._config_lines[name], self.config_directory / self._CONFIG_FILENAMES[name])

    @property
    def cache_config(self) -> ConfigFile:
        return self._line_config("cache_config")

    @property
    def hosting_config(self) -> ConfigFile:
        return self._line_config("hosting_config")

    @property
    def ip_allow_config(self) -> ConfigFile:
        return self._line_config("ip_allow_lines")

    @property
    def parent_config(self) -> ConfigFile:
        return self._line_config("parent_config")

    @property
    def plugin_config(self) -> ConfigFile:
        return self._line_config("plugin_config")

    @property
    def remap_config(self) -> ConfigFile:
        return self._line_config("remap_config")

    @property
    def remap_yaml(self) -> ConfigFile:
        return self._line_config("remap_yaml_lines")

    @property
    def splitdns_config(self) -> ConfigFile:
        return self._line_config("splitdns_config")

    @property
    def ssl_multicert_config(self) -> ConfigFile:
        return self._line_config("ssl_multicert_lines")

    @property
    def storage_config(self) -> ConfigFile:
        return self._line_config("storage_lines")

    @property
    def is_running(self) -> bool:
        """Return whether Traffic Server is running."""

        return self._process is not None and self._process.return_code is None

    @property
    def process_output(self) -> str:
        """Return stdout and stderr captured outside ATS's bound traffic.out."""

        return "" if self._process is None else self._process.output()

    def start(self) -> None:
        """Materialize configuration and wait until Traffic Server is ready."""

        if self._was_started:
            raise RuntimeError(f"{self.name} has already been started")
        if self._context.use_uds:
            server_ports = str(self._records.get("proxy.config.http.server_ports", self.http_port))
            if self._process_options.get("enable_tls", False):
                server_ports += f" {self.https_port}:ssl"
            if self._process_options.get("enable_quic", False):
                server_ports += f" {self.https_port}:quic"
            if self._process_options.get("enable_proxy_protocol", False):
                client_flag = ":pp-clnt" if self._process_options.get("enable_proxy_protocol_cp_src", False) else ""
                server_ports += f" {self.proxy_protocol_port}:pp{client_flag}"
                if self._process_options.get("enable_tls", False):
                    server_ports += f" {self.proxy_protocol_https_port}:ssl:pp{client_flag}"
            if self.uds_path not in server_ports:
                server_ports += f" {self.uds_path}"
            self._records["proxy.config.http.server_ports"] = server_ports
        ats_config = {
            "name": self.name,
            "process_config": self._process_options,
            "records_config": self._records,
            **{
                name: lines for name, lines in self._config_lines.items() if lines
            },
            **self._config,
        }
        self._runner.spec.urtest["ats"] = ats_config
        self._process = self._runner._start_ats()
        self._was_started = True

    def expect_start_failure(self, expression: str, return_code: int | Sequence[int] = 33) -> None:
        """Expect startup to fail with @a return_code and a matching diagnostic."""

        if self._was_started:
            raise RuntimeError(f"{self.name} has already been started")
        self._config["startup_failure"] = expression
        self._config["return_code"] = list(return_code) if not isinstance(return_code, int) else return_code
        self._allow_fatal_diagnostics = True

    def stop(self) -> None:
        """Stop Traffic Server without relinquishing fixture ownership."""

        if self._process is not None:
            self._process.stop()

    def wait(self, timeout: float = 60) -> None:
        """Wait for Traffic Server to exit and validate its configured return code."""

        if self._process is None:
            raise RuntimeError(f"{self.name} has not been started")
        self._process.wait(timeout)

    def kill(self) -> None:
        """Kill Traffic Server without running its shutdown handlers."""

        if self._process is None or self._process._process is None or self._process.return_code is not None:
            return
        os.killpg(self._process._process.pid, signal.SIGKILL)
        self._process._process.wait(timeout=5)
        self._process._close_streams()

    def send_signal(self, signal_number: int) -> None:
        """Send @a signal_number to the Traffic Server process."""

        if self._process is None:
            raise RuntimeError(f"{self.name} has not been started")
        self._process.send_signal(signal_number)

    def drain_and_stop(self, delay: float = 1.0) -> None:
        """Drain Traffic Server and stop it cleanly."""

        result = self.traffic_ctl("server", "drain")
        if result.returncode != 0:
            raise RuntimeError(f"Could not drain {self.name}:\n{result.output}")
        if delay:
            time.sleep(delay)
        self.stop()

    def run(self, *arguments: str | Path, timeout: float = 30) -> CommandResult:
        """Run a command with this ATS instance's environment."""

        command = tuple(str(argument) for argument in arguments)
        completed = subprocess.run(
            command,
            cwd=self.run_directory,
            env=self.environment,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
        return CommandResult(command, completed.returncode, completed.stdout, completed.stderr)

    def run_shell(self, script: str, timeout: float = 30) -> CommandResult:
        """Run a Bash script with this ATS instance's environment."""

        return self.run("/bin/bash", "-o", "pipefail", "-c", script, timeout=timeout)

    def traffic_ctl(self, *arguments: str, timeout: float = 30) -> CommandResult:
        """Run traffic_ctl against this ATS run root."""

        return self.run("traffic_ctl", *arguments, timeout=timeout)

    def rpc(self, request: object, timeout: float = 30) -> CommandResult:
        """Send one JSON-RPC request through traffic_ctl."""

        request_path = self.run_directory / "request.json"
        request_path.write_text(request if isinstance(request, str) else json.dumps(request))
        return self.traffic_ctl(
            "rpc",
            "file",
            str(request_path),
            "--run-root",
            str(self.config_directory / "runroot.yaml"),
            "--format",
            "json",
            timeout=timeout,
        )

    def add_default_ssl_files(self) -> None:
        """Enable TLS using Uranium's default certificate and key."""

        self._process_options["enable_tls"] = True

    def copy_to_config(self, *paths: str | Path) -> None:
        """Copy source files into ATS's configuration directory on start."""

        copies = self._config.setdefault("copy_to_config_dir", [])
        copies.extend({"source": str(path), "destination": Path(path).name} for path in paths)

    def copy_to_ssl(self, *paths: str | Path) -> None:
        """Copy certificate material into ATS's SSL directory on start."""

        self._config.setdefault("copy_to_ssl_dir", []).extend(str(path) for path in paths)

    def copy_custom_plugin(self, path: str | Path) -> None:
        """Copy a test plugin into ATS's plugin directory on start."""

        self._config.setdefault("copy_custom_plugin", []).append(str(path))

    def write_config_file(self, filename: str, content: str) -> None:
        """Materialize a test-owned ATS configuration file on start."""

        self._config.setdefault("inline_config_files", {})[filename] = content

    def write_runtime_file(self, filename: str, content: str) -> None:
        """Materialize a test-owned ATS local-state file on start."""

        self._config.setdefault("inline_runtime_files", {})[filename] = content

    def write_body_factory_file(self, relative_path: str, content: str) -> None:
        """Materialize a custom body-factory template on start."""

        self._config.setdefault("body_factory_files", {})[relative_path] = content

    def allow_private_connect(self, methods: Sequence[str] = ("CONNECT",)) -> None:
        """Allow outbound test traffic to loopback addresses for @a methods."""

        method_lines = "\n".join(f"      - {method}" for method in methods)
        self.write_config_file(
            "ip_allow.yaml",
            "ip_allow:\n"
            "  - apply: in\n"
            "    ip_addrs: [0/0, ::/0]\n"
            "    action: allow\n"
            "    methods: ALL\n"
            "  - apply: out\n"
            "    ip_addrs: [127.0.0.0/8, ::1]\n"
            "    action: allow\n"
            "    methods:\n"
            f"{method_lines}\n",
        )

    def append_records_document(self, values: Mapping[str, object]) -> None:
        """Append another records.yaml document after the initial configuration."""

        self._config.setdefault("records_documents", []).append(dict(values))

    def set_logging_yaml(self, document: Mapping[str, object]) -> None:
        """Write a structured logging.yaml configuration when ATS starts."""

        self._config["logging_yaml"] = dict(document)

    def set_ssl_multicert_yaml(self, document: Mapping[str, object]) -> None:
        """Write structured TLS certificate configuration without installing defaults."""

        self._config["ssl_multicert_yaml"] = dict(document)

    def set_startup_timeout(self, timeout: float) -> None:
        """Set the maximum number of seconds allowed for ATS startup."""

        self._config["startup_timeout"] = timeout

    def set_environment(self, name: str, value: str) -> None:
        """Set one environment variable for Traffic Server and its tools."""

        self._config.setdefault("environment", {})[name] = value

    def unset_environment(self, *names: str) -> None:
        """Remove inherited or harness-provided variables before ATS starts."""

        self._config.setdefault("unset_environment", []).extend(names)

    def omit_config_file(self, filename: str) -> None:
        """Leave @a filename absent from the materialized ATS configuration."""

        self._config.setdefault("omit_config_files", []).append(filename)

    def has_feature(self, feature: str) -> bool:
        """Return whether ATS was built with @a feature."""

        return bool(self._context.runtime.features.get(feature))

    def plugin_exists(self, name: str) -> bool:
        """Return whether an installed ATS plugin is available."""

        filename = name if name.endswith(".so") else f"{name}.so"
        return (Path(self._context.runtime.layout["PLUGINDIR"]) / filename).is_file()

    def close(self) -> None:
        """Stop Traffic Server and validate fatal diagnostics."""

        self.stop()
        if self._was_started and self.diags_log.exists():
            content = self.diags_log.read_text(errors="replace")
            if "FATAL:" in content and not self._allow_fatal_diagnostics:
                raise AssertionError(f"{self.name} emitted a fatal diagnostic:\n{content}")


class ATSFactory:
    """Create Traffic Server instances owned by one pytest scenario."""

    def __init__(self, context: ProceduralContext) -> None:
        self._context = context
        self._services: list[ATS] = []
        self._names: set[str] = set()

    @property
    def run_directory(self) -> Path:
        """Return the test's isolated sandbox directory."""

        return self._context.run_directory

    def has_feature(self, feature: str) -> bool:
        """Return whether ATS was built with @a feature."""

        return bool(self._context.runtime.features.get(feature))

    def create(self, name: str | None = None, **process_options: Any) -> ATS:
        """Create a named Traffic Server with fixture-owned cleanup."""

        process_name = name or f"ats{len(self._services) + 1}"
        if process_name in self._names:
            raise ValueError(f"Traffic Server process {process_name!r} already exists")
        service = ATS(self._context, process_name, **process_options)
        self._services.append(service)
        self._names.add(process_name)
        return service

    def close(self) -> None:
        """Stop and validate every created instance in reverse order."""

        failures = []
        for service in reversed(self._services):
            try:
                service.close()
            except Exception as error:
                failures.append(error)
        self._services.clear()
        self._names.clear()
        if failures:
            raise ExceptionGroup("Traffic Server cleanup failed", failures)


class Curl:
    """Run curl requests and return ordinary Python result objects."""

    def __init__(self, working_directory: Path, *, use_uds: bool = False) -> None:
        """Create a curl command runner.

        :param working_directory: Directory in which curl commands execute.
        :param use_uds: Whether requests targeting ATS should use its Unix
            domain socket instead of its TCP listener.
        """

        self._working_directory = working_directory
        self._use_uds = use_uds

    @property
    def uses_uds(self) -> bool:
        return self._use_uds

    @staticmethod
    def supports(feature: str) -> bool:
        """Return whether the installed curl advertises a feature.

        :param feature: Case-insensitive feature name to find in
            ``curl --version`` output.
        """

        result = subprocess.run(("curl", "--version"), capture_output=True, text=True, check=False)
        return feature.lower() in (result.stdout + result.stderr).lower()

    def get(
        self,
        ats: ATS,
        path: str = "/",
        *,
        headers: Mapping[str, str] | None = None,
        options: str = "",
        timeout: float = 30,
    ) -> CommandResult:
        """Send a GET request to an ATS instance.

        :param ats: ATS instance that receives the request.
        :param path: Request path, with or without a leading slash.
        :param headers: HTTP headers to add to the request.
        :param options: Shell-style curl arguments. Quoting is parsed with
            :func:`shlex.split`; no shell is invoked.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        request_path = path if path.startswith("/") else f"/{path}"
        arguments = []
        if self._use_uds:
            arguments.extend(["--unix-socket", ats.uds_path])
        arguments.extend(shlex.split(options))
        for name, value in (headers or {}).items():
            arguments.extend(["--header", f"{name}: {value}"])
        arguments.append(f"http://127.0.0.1:{ats.http_port}{request_path}")
        return self._run(arguments, timeout=timeout)

    def run_for(self, ats: ATS, arguments: str, timeout: float = 30) -> CommandResult:
        """Run curl arguments using the selected ATS transport.

        :param ats: ATS instance whose Unix socket is used when UDS mode is
            enabled.
        :param arguments: Shell-style curl arguments. Quoting is parsed with
            :func:`shlex.split`; no shell is invoked.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        command_arguments = []
        if self._use_uds:
            command_arguments.extend(("--unix-socket", ats.uds_path))
        command_arguments.extend(shlex.split(arguments))
        return self._run(command_arguments, timeout=timeout)

    def run_script(self, ats: ATS, script: str, timeout: float = 30) -> CommandResult:
        """Run a Bash script containing curl command placeholders.

        :param ats: ATS instance whose Unix socket is substituted for
            ``{curl}`` when UDS mode is enabled.
        :param script: Bash script in which ``{curl}`` is the transport-aware
            curl command and ``{curl_base}`` is plain curl.
        :param timeout: Maximum number of seconds to wait for the script.
        """

        curl_command = "curl"
        if self._use_uds:
            curl_command += f" --unix-socket {shlex.quote(ats.uds_path)}"
        rendered = script.replace("{curl}", curl_command).replace("{curl_base}", "curl")
        command = ("/bin/bash", "-o", "pipefail", "-c", rendered)
        completed = subprocess.run(
            command,
            cwd=self._working_directory,
            capture_output=True,
            text=True,
            timeout=timeout,
            shell=False,
            check=False,
        )
        return CommandResult(command, completed.returncode, completed.stdout, completed.stderr)

    def run(self, arguments: str, timeout: float = 30) -> CommandResult:
        """Run curl without applying an ATS transport.

        :param arguments: Shell-style curl arguments. Quoting is parsed with
            :func:`shlex.split`; no shell is invoked.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        return self._run(shlex.split(arguments), timeout=timeout)

    def _run(self, arguments: Sequence[str], *, timeout: float) -> CommandResult:
        """Execute an already tokenized curl command.

        :param arguments: Curl argument vector without the executable name.
        :param timeout: Maximum number of seconds to wait for curl.
        """

        command = ("curl", *arguments)
        completed = subprocess.run(
            command,
            cwd=self._working_directory,
            capture_output=True,
            text=True,
            timeout=timeout,
            shell=False,
            check=False,
        )
        return CommandResult(command, completed.returncode, completed.stdout, completed.stderr)


class ProcessService:
    """A pytest-owned non-ATS process used by a procedural test."""

    def __init__(
        self,
        process: ManagedProcess,
        *,
        reject_expression: str = "",
        ready_port: int = 0,
        ready_address: str = "127.0.0.1",
    ) -> None:
        self._process = process
        self._reject_expression = reject_expression
        self._ready_port = ready_port
        self._ready_address = ready_address
        self._was_started = False
        self._was_validated = False

    @property
    def name(self) -> str:
        return self._process.name

    @property
    def run_directory(self) -> Path:
        return self._process.run_directory

    @property
    def is_running(self) -> bool:
        return self._process.return_code is None if self._was_started else False

    @property
    def stdout(self) -> str:
        self._process._flush_streams()
        return self._process.stdout_path.read_text(errors="replace") if self._process.stdout_path.exists() else ""

    @property
    def stderr(self) -> str:
        self._process._flush_streams()
        return self._process.stderr_path.read_text(errors="replace") if self._process.stderr_path.exists() else ""

    @property
    def output(self) -> str:
        return self.stdout + self.stderr

    def start(self) -> None:
        self._process.start()
        self._was_started = True
        if self._ready_port:
            self._process.wait_until(
                lambda: _tcp_open(self._ready_port, self._ready_address),
                10,
                f"{self.name} listener on {self._ready_address}:{self._ready_port}",
            )

    def wait(self, timeout: float = 60) -> CommandResult:
        self._process.wait(timeout)
        self._validate_output()
        self._was_validated = True
        return CommandResult(tuple(self._process.command), int(self._process.return_code or 0), self.stdout, self.stderr)

    def run(self, timeout: float = 60) -> CommandResult:
        self.start()
        return self.wait(timeout)

    def stop(self) -> None:
        self._process.stop()

    def close(self) -> None:
        self.stop()
        if self._was_started and not self._was_validated:
            self._validate_output()

    def _validate_output(self) -> None:
        if self._reject_expression and re.search(self._reject_expression, self.output):
            raise AssertionError(f"Unexpected diagnostic in {self.name}:\n{self.output}")


class OriginServer(ProcessService):
    """A pytest-owned microserver origin."""

    def __init__(
        self,
        process: ManagedProcess,
        port: int,
        https_port: int,
        data_directory: Path,
        address: str,
    ) -> None:
        super().__init__(process)
        self._port = port
        self._https_port = https_port
        self._data_directory = data_directory
        self._address = address

    @property
    def port(self) -> int:
        return self._port

    @property
    def http_port(self) -> int:
        return self._port

    @property
    def https_port(self) -> int:
        return self._https_port

    def start(self) -> None:
        super().start()
        ready_address = "127.0.0.1" if self._address == "0.0.0.0" else self._address
        ports = [port for port in (self.http_port, self.https_port) if port]
        self._process.wait_until(
            lambda: all(_tcp_open(port, ready_address) for port in ports),
            10,
            f"microserver listeners on {ready_address}:{','.join(str(port) for port in ports)}",
        )

    def add_response(self, request: Mapping[str, Any], response: Mapping[str, Any], filename: str = "sessionlog.json") -> None:
        try:
            from trlib import Request, Response, Session, Transaction
        except ImportError as error:
            raise RuntimeError("traffic-replay is required for microserver scenarios") from error
        request_value = Request.fromRequestLine(request["headers"], request.get("body", ""), request.get("options"))
        response_value = Response.fromRequestLine(response["headers"], response.get("body", ""), response.get("options"))
        transaction = Transaction(request_value, None, response_value, None, None, None)
        path = self._data_directory / filename
        if path.exists():
            document = json.loads(path.read_text())
            document["sessions"][0]["transactions"].append(transaction.toJSON())
        else:
            document = {
                "sessions": [Session(filename, None, None, [transaction]).toJSON()],
                "meta": {
                    "version": "1.0"
                },
            }
        path.write_text(json.dumps(document))


class DNSServer(ProcessService):
    """A pytest-owned microDNS server."""

    def __init__(self, process: ManagedProcess, port: int, zone_file: Path) -> None:
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
        document = json.loads(self._zone_file.read_text())
        for hostname, addresses in records.items():
            document["mappings"].append({hostname if hostname.endswith(".") else hostname + ".": list(addresses)})
        self._zone_file.write_text(json.dumps(document))


class HttpBinServer(ProcessService):
    """A pytest-owned go-httpbin server."""

    def __init__(self, process: ManagedProcess, port: int) -> None:
        super().__init__(process)
        self._port = port

    @property
    def port(self) -> int:
        return self._port

    def start(self) -> None:
        super().start()
        self._process.wait_until(lambda: _tcp_open(self.port), 10, f"HTTPBin listener on 127.0.0.1:{self.port}")


class VerifierServer(ProcessService):
    """A pytest-owned Proxy Verifier server."""

    def __init__(self, process: ManagedProcess, http_port: int, https_port: int) -> None:
        super().__init__(process, reject_expression="Violation")
        self._http_port = http_port
        self._https_port = https_port

    @property
    def http_port(self) -> int:
        return self._http_port

    @property
    def https_port(self) -> int:
        return self._https_port

    def start(self) -> None:
        super().start()
        port = self.http_port or self.https_port
        self._process.wait_until(lambda: _tcp_open(port), 10, f"Proxy Verifier listener on 127.0.0.1:{port}")


class ServiceFactory:
    """Create pytest-owned support processes for procedural tests."""

    def __init__(self, context: ProceduralContext) -> None:
        self._context = context
        self._services: list[ProcessService] = []
        self._names: set[str] = set()

    def _directory(self, name: str) -> Path:
        if name in self._names:
            raise ValueError(f"Support process {name!r} already exists")
        self._names.add(name)
        directory = self._context.run_directory / name
        directory.mkdir(parents=True)
        return directory

    def allocate_port(self, socket_type: int = socket.SOCK_STREAM) -> int:
        """Reserve a listener port for a bespoke support process."""

        return self._context.runtime.allocate_port(socket_type)

    def resolve_path(self, value: str | Path) -> Path:
        """Resolve a source path, placeholder, or generated test artifact."""

        return self._context.resolve_path(value)

    def origin(self, name: str, **options: Any) -> OriginServer:
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
        service.add_response(
            {"headers": "GET /ruok HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"},
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
        directory = self._directory(name)
        http_ports = list(options.pop("http_ports", [self._context.runtime.allocate_port()]))
        https_ports = list(options.pop("https_ports", [self._context.runtime.allocate_port()]))
        ssl_dir = self._context.runtime.test_tools / "proxy-verifier" / "ssl"
        command: list[str | Path | int] = [self._context.runtime.verifier_bin / "verifier-server", "run"]
        if http_ports:
            command.extend(["--listen-http", _addresses(http_ports)])
        if https_ports:
            command.extend(["--listen-https", _addresses(https_ports)])
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
            command.extend(["--connect-http", _addresses(http_ports)])
        if https_ports:
            command.extend(["--connect-https", _addresses(https_ports)])
        if http3_ports:
            command.extend(["--connect-http3", _addresses(http3_ports)])
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
        """Create an arbitrary managed process for a bespoke scenario."""

        directory = self._directory(name)
        process = ManagedProcess(name, command, directory, environment, expected_return_codes)
        return self._remember(ProcessService(process, ready_port=ready_port, ready_address=ready_address))

    def proxy_verifier_at_least(self, version: str) -> bool:
        binary = self._context.runtime.verifier_bin / "verifier-client"
        result = subprocess.run((str(binary), "--version"), capture_output=True, text=True, check=False)
        found = re.search(r"\d+(?:\.\d+)+", result.stdout + result.stderr)
        return found is not None and _version_tuple(found.group()) >= _version_tuple(version)

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
        self._services.append(service)
        return service


def assert_matches_gold(actual: str, expected: Path) -> None:
    """Assert that @a actual matches a Uranium wildcard gold file."""

    expected_text = expected.read_text(errors="replace").replace("\r\n", "\n")
    expected_text = expected_text.replace("\n``\n", "``")
    actual_text = actual.replace("\r\n", "\n")
    pattern = "\\A" + ".*?".join(re.escape(part) for part in re.split(r"(?:\{\}|``)", expected_text)) + "\\Z"
    if re.match(pattern, actual_text, re.DOTALL) is None:
        difference = "".join(
            difflib.unified_diff(expected_text.splitlines(True), actual_text.splitlines(True), str(expected), "actual"))
        raise AssertionError(f"Output did not match gold file:\n{difference}")


def wait_for_file_lines(path: Path, expression: str, count: int, timeout: float = 10) -> str:
    """Wait until @a path contains @a count lines matching @a expression."""

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
    """Send bytes to a TCP listener and return everything received before close."""

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
    """Wait until an ATS metric reaches @a expected and return its value."""

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


def _addresses(ports: Sequence[int]) -> str:
    return ",".join(f"127.0.0.1:{port}" for port in ports)


def _tcp_open(port: int, address: str = "127.0.0.1") -> bool:
    try:
        with socket.create_connection((address, port), timeout=0.1):
            return True
    except OSError:
        return False


def _version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in re.findall(r"\d+", value))
