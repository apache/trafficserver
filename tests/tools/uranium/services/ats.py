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
"""Traffic Server process and configuration services."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from pathlib import Path
import json
import os
import signal
import subprocess
import time
from typing import Any

from ..config import ReplaySpec
from ..process import ManagedProcess
from ..replay import ReplayTest
from .context import CommandResult, ProceduralContext


class RecordsConfig:
    """Stage records.yaml updates until ATS starts."""

    def __init__(self, values: dict[str, object]) -> None:
        """Wrap the dictionary that will become records.yaml.

        :param values: Mutable record-name-to-value mapping to stage.
        """

        self._values = values

    def update(self, values: Mapping[str, object]) -> None:
        """Merge flat ATS record names into the staged values.

        :param values: Record names and values to merge.
        """

        self._values.update(values)


class ConfigFile:
    """Stage one line-oriented ATS configuration file."""

    def __init__(self, lines: list[str], path: Path) -> None:
        """Wrap the staged lines for one configuration file.

        :param lines: Mutable list receiving staged configuration lines.
        :param path: Destination path used when ATS starts.
        """

        self._lines = lines
        self._path = path

    @property
    def path(self) -> Path:
        """Return the configuration file's materialized path."""

        return self._path

    def add_line(self, line: object) -> None:
        """Append one line to the staged file.

        :param line: Value converted to one configuration line.
        """

        self._lines.append(str(line))

    def add_lines(self, lines: Sequence[object] | str) -> None:
        """Append lines from a string or sequence.

        :param lines: Newline-delimited text or values for separate lines.
        """

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
        """Create a staged Traffic Server instance.

        :param context: Runtime and sandbox state for the pytest item.
        :param name: Unique process name within the scenario.
        :param process_options: Replay-runner options controlling ATS startup.
        """

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
        """Return the staged line-oriented configuration named by a key.

        :param name: Key in :attr:`_CONFIG_FILENAMES`.
        """

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
        """Expect startup to fail with a matching diagnostic.

        :param expression: Regular expression required in startup output.
        :param return_code: Accepted process return code or codes.
        """

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
        """Wait for Traffic Server to exit and validate its return code.

        :param timeout: Maximum number of seconds to wait.
        """

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
        """Send a signal to the Traffic Server process.

        :param signal_number: Operating-system signal number to send.
        """

        if self._process is None:
            raise RuntimeError(f"{self.name} has not been started")
        self._process.send_signal(signal_number)

    def drain_and_stop(self, delay: float = 1.0) -> None:
        """Drain Traffic Server and stop it cleanly.

        :param delay: Number of seconds between draining and stopping.
        """

        result = self.traffic_ctl("server", "drain")
        if result.returncode != 0:
            raise RuntimeError(f"Could not drain {self.name}:\n{result.output}")
        if delay:
            time.sleep(delay)
        self.stop()

    def run(self, *arguments: str | Path, timeout: float = 30) -> CommandResult:
        """Run a command with this ATS instance's environment.

        :param arguments: Executable followed by its command-line arguments.
        :param timeout: Maximum number of seconds to wait for completion.
        """

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
        """Run a Bash script with this ATS instance's environment.

        :param script: Bash source to execute.
        :param timeout: Maximum number of seconds to wait for completion.
        """

        return self.run("/bin/bash", "-o", "pipefail", "-c", script, timeout=timeout)

    def traffic_ctl(self, *arguments: str, timeout: float = 30) -> CommandResult:
        """Run traffic_ctl against this ATS run root.

        :param arguments: Arguments passed to traffic_ctl.
        :param timeout: Maximum number of seconds to wait for completion.
        """

        return self.run("traffic_ctl", *arguments, timeout=timeout)

    def rpc(self, request: object, timeout: float = 30) -> CommandResult:
        """Send one JSON-RPC request through traffic_ctl.

        :param request: JSON string or JSON-serializable request value.
        :param timeout: Maximum number of seconds to wait for completion.
        """

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
        """Copy source files into ATS's configuration directory on start.

        :param paths: Source paths copied when ATS starts.
        """

        copies = self._config.setdefault("copy_to_config_dir", [])
        copies.extend({"source": str(path), "destination": Path(path).name} for path in paths)

    def copy_to_ssl(self, *paths: str | Path) -> None:
        """Copy certificate material into ATS's SSL directory on start.

        :param paths: Certificate or key paths copied when ATS starts.
        """

        self._config.setdefault("copy_to_ssl_dir", []).extend(str(path) for path in paths)

    def copy_custom_plugin(self, path: str | Path) -> None:
        """Copy a test plugin into ATS's plugin directory on start.

        :param path: Test plugin path to copy.
        """

        self._config.setdefault("copy_custom_plugin", []).append(str(path))

    def write_config_file(self, filename: str, content: str) -> None:
        """Materialize a test-owned ATS configuration file on start.

        :param filename: Destination filename under the config directory.
        :param content: Complete file contents.
        """

        self._config.setdefault("inline_config_files", {})[filename] = content

    def write_runtime_file(self, filename: str, content: str) -> None:
        """Materialize a test-owned ATS local-state file on start.

        :param filename: Destination filename under the runtime directory.
        :param content: Complete file contents.
        """

        self._config.setdefault("inline_runtime_files", {})[filename] = content

    def write_body_factory_file(self, relative_path: str, content: str) -> None:
        """Materialize a custom body-factory template on start.

        :param relative_path: Destination path below the body-factory root.
        :param content: Complete template contents.
        """

        self._config.setdefault("body_factory_files", {})[relative_path] = content

    def allow_private_connect(self, methods: Sequence[str] = ("CONNECT",)) -> None:
        """Allow outbound test traffic to loopback addresses.

        :param methods: HTTP methods allowed to connect to loopback origins.
        """

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
        """Append another records.yaml document after the initial configuration.

        :param values: Flat record names and values for the appended document.
        """

        self._config.setdefault("records_documents", []).append(dict(values))

    def set_logging_yaml(self, document: Mapping[str, object]) -> None:
        """Write a structured logging.yaml configuration when ATS starts.

        :param document: Parsed logging.yaml document to serialize.
        """

        self._config["logging_yaml"] = dict(document)

    def set_ssl_multicert_yaml(self, document: Mapping[str, object]) -> None:
        """Write structured TLS certificate configuration without defaults.

        :param document: Parsed ssl_multicert.yaml document to serialize.
        """

        self._config["ssl_multicert_yaml"] = dict(document)

    def set_startup_timeout(self, timeout: float) -> None:
        """Set the maximum time allowed for ATS startup.

        :param timeout: Startup timeout in seconds.
        """

        self._config["startup_timeout"] = timeout

    def set_environment(self, name: str, value: str) -> None:
        """Set one environment variable for Traffic Server and its tools.

        :param name: Environment variable name.
        :param value: Environment variable value.
        """

        self._config.setdefault("environment", {})[name] = value

    def unset_environment(self, *names: str) -> None:
        """Remove environment variables before ATS starts.

        :param names: Inherited or harness-provided variable names to remove.
        """

        self._config.setdefault("unset_environment", []).extend(names)

    def omit_config_file(self, filename: str) -> None:
        """Leave a file absent from the materialized ATS configuration.

        :param filename: Configuration filename to omit.
        """

        self._config.setdefault("omit_config_files", []).append(filename)

    def has_feature(self, feature: str) -> bool:
        """Return whether ATS was built with a feature.

        :param feature: Build-feature name to query.
        """

        return bool(self._context.runtime.features.get(feature))

    def plugin_exists(self, name: str) -> bool:
        """Return whether an installed ATS plugin is available.

        :param name: Plugin basename, with or without the ``.so`` suffix.
        """

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
        """Create a factory bound to one procedural scenario.

        :param context: Runtime and sandbox state for the pytest item.
        """

        self._context = context
        self._services: list[ATS] = []
        self._names: set[str] = set()

    @property
    def run_directory(self) -> Path:
        """Return the test's isolated sandbox directory."""

        return self._context.run_directory

    def has_feature(self, feature: str) -> bool:
        """Return whether ATS was built with a feature.

        :param feature: Build-feature name to query.
        """

        return bool(self._context.runtime.features.get(feature))

    def create(self, name: str | None = None, **process_options: Any) -> ATS:
        """Create a named Traffic Server with fixture-owned cleanup.

        :param name: Unique process name, or ``None`` for an automatic name.
        :param process_options: Replay-runner options controlling ATS startup.
        """

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
