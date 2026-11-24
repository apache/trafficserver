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
import copy
import grp
import json
import os
import pwd
import re
import signal
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any

import yaml

from ..config import ReplayConfigError, format_plugin_entry, merge_flat_records, return_codes, write_yaml
from ..process import ManagedProcess
from ..utils import tcp_open
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
    """One Traffic Server instance: its sandboxed run root, configuration, process, and diagnostics.

    Procedural tests stage configuration through attributes such as
    ``records`` and ``remap_config``; direct replays hand over a
    manifest's ``urtest.ats`` mapping through :meth:`from_manifest`. Both are
    staged in that mapping's schema, so :meth:`start` builds every instance
    the same way.
    """

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
        self._test_directory = context.test_directory
        self._process_options = dict(process_options)
        return_code = self._process_options.pop("return_code", None)
        self._records: dict[str, object] = {}
        self._config_lines = {key: [] for key in self._CONFIG_FILENAMES}
        self._config: dict[str, Any] = {}
        if return_code is not None:
            self._config["return_code"] = return_code
        runtime = context.runtime
        self._http_port = runtime.allocate_port()
        self._https_port = runtime.allocate_port()
        self._proxy_protocol_port = runtime.allocate_port()
        self._proxy_protocol_https_port = runtime.allocate_port()
        self._manager_port = runtime.allocate_port()
        self._admin_port = runtime.allocate_port()
        self._ipv6_port = runtime.allocate_port()
        self._ipv6_https_port = runtime.allocate_port()
        self._name = name
        self._root = context.run_directory / name
        self._paths: dict[str, Path] = {}
        self._environment: dict[str, str] | None = None
        self._nameserver_port: int | None = None
        self._placeholders: dict[str, str] = {}
        self._temporary_directories: list[Path] = []
        self._process: ManagedProcess | None = None
        self._was_started = False
        self._exit_was_expected = False
        self._uds_directory: Path | None = None
        self._allow_fatal_diagnostics = False
        self._allowed_errors: list[re.Pattern[str]] = []
        self.records = RecordsConfig(self._records)

    @classmethod
    def from_manifest(cls, context: ProceduralContext, config: Mapping[str, Any], *, test_directory: Path) -> ATS:
        """Create an unstarted instance configured by a replay manifest's ``urtest.ats`` mapping.

        :param context: Runtime and the replay's sandbox, which holds this
            instance's run root.
        :param config: The manifest's ``urtest.ats`` mapping; it is copied, not
            modified.
        :param test_directory: Directory that relative manifest paths, such as
            ``copy_to_config_dir`` entries, are resolved against.
        """

        settings = copy.deepcopy(dict(config))
        ats = cls(context, str(settings.pop("name", "ts")), **settings.pop("process_config", {}))
        ats._test_directory = test_directory
        ats._records.update(settings.pop("records_config", {}))
        ats._config.update(settings)
        if "startup_failure" in settings:
            ats._allow_fatal_diagnostics = True
        return ats

    @property
    def name(self) -> str:
        """Return this Traffic Server instance's process name."""

        return self._name

    @property
    def http_port(self) -> int:
        """Return the selected IPv4 HTTP port."""

        return self._http_port

    @property
    def https_port(self) -> int:
        """Return the selected IPv4 TLS port."""

        return self._https_port

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

        return self._proxy_protocol_port

    @property
    def proxy_protocol_https_port(self) -> int:
        """Return the selected TLS Proxy Protocol port."""

        return self._proxy_protocol_https_port

    @property
    def uds_path(self) -> str:
        """Return this instance's Unix-domain listener path."""

        path = self._root / "runtime" / "ats.sock"
        # Unix socket addresses have a small byte limit regardless of the
        # filesystem's support for long, readable sandbox names.
        if len(os.fsencode(path)) >= 104:
            if self._uds_directory is None:
                self._uds_directory = Path(tempfile.mkdtemp(prefix="ats-uds-", dir="/tmp"))
                chown_for_ats(self._uds_directory)
            path = self._uds_directory / "ats.sock"
        return str(path)

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
        """Return the configured diagnostic file or captured diagnostic stream."""

        destination = str(self._records.get("proxy.config.diags.logfile.filename", "diags.log"))
        if destination in ("stdout", "stderr"):
            if self._process_options.get("capture_traffic_out", True):
                return self.traffic_out
            return self._root / f"{self.name}.{destination}"
        return self.log_directory / destination

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

        if self._environment is None:
            raise RuntimeError(f"{self.name} has not been started")
        return self._environment

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
    def return_code(self) -> int | None:
        """Return Traffic Server's exit status, or None while it runs or before it starts."""

        return None if self._process is None else self._process.return_code

    @property
    def process_output(self) -> str:
        """Return stdout and stderr captured outside ATS's bound traffic.out."""

        return "" if self._process is None else self._process.output()

    def start(self) -> None:
        """Materialize configuration and wait until Traffic Server is ready."""

        if self._was_started:
            raise RuntimeError(f"{self.name} has already been started")
        if self._context.use_uds:
            # Without UDS, a test's own server_ports replaces the runner's
            # default listeners outright, so only the socket is appended to it.
            # Appending the TLS, QUIC or PROXY protocol defaults as well would
            # list a port twice when the test already names it, or open one
            # the test left out.
            if "proxy.config.http.server_ports" in self._records:
                server_ports = str(self._records["proxy.config.http.server_ports"])
            else:
                # The defaults _launch writes, so the two cannot drift.
                server_ports = self._server_ports(
                    enable_tls=bool(self._process_options.get("enable_tls", False)),
                    enable_quic=bool(self._process_options.get("enable_quic", False)),
                    enable_proxy_protocol=bool(self._process_options.get("enable_proxy_protocol", False)),
                    enable_proxy_protocol_cp_src=bool(self._process_options.get("enable_proxy_protocol_cp_src", False)),
                )
            if self.uds_path not in server_ports:
                server_ports += f" {self.uds_path}"
            self._records["proxy.config.http.server_ports"] = server_ports
        ats_config = {
            "name": self.name,
            "process_config": self._process_options,
            "records_config": self._records,
            **{name: lines for name, lines in self._config_lines.items() if lines},
            **self._config,
        }
        self._process = self._launch(ats_config)
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

    def chown_for_ats(self, path: Path) -> None:
        """Give files a test created to the user Traffic Server runs as.

        This does nothing unless the test runs as root, in which case Traffic
        Server switches to an unprivileged user that cannot read root-owned
        0600 files such as generated keys.

        :param path: Directory whose contents are handed to the ATS user.
        """

        chown_for_ats(path)

    def allow_errors(self, expression: str) -> None:
        """Accept diags.log ``ERROR:`` lines matching a regular expression.

        Errors that do not match any allowed expression still fail the test.

        :param expression: Regular expression searched for in each error line.
        """

        self._allowed_errors.append(re.compile(expression))

    def stop(self) -> None:
        """Stop Traffic Server without relinquishing fixture ownership."""

        if self._process is not None:
            if self._was_started and not self._exit_was_expected and not self._allow_fatal_diagnostics:
                if self._process.return_code is not None:
                    message = f"{self.name} exited unexpectedly with status {self._process.return_code}.\n{self.process_output}"
                    self._process.stop()
                    raise AssertionError(message)
            self._exit_was_expected = True
            self._process.stop()

    def wait(self, timeout: float = 60) -> None:
        """Wait for Traffic Server to exit and validate its return code.

        :param timeout: Maximum number of seconds to wait.
        """

        if self._process is None:
            raise RuntimeError(f"{self.name} has not been started")
        self._process.wait(timeout)
        self._exit_was_expected = True

    def kill(self) -> None:
        """Kill Traffic Server without running its shutdown handlers."""

        if self._process is None or self._process._process is None or self._process.return_code is not None:
            return
        self._exit_was_expected = True
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
        # Clients such as openssl s_client echo binary protocol data, e.g. an
        # HTTP/2 SETTINGS frame, which must not fail the decode.
        completed = subprocess.run(
            command,
            cwd=self.run_directory,
            env=self.environment,
            capture_output=True,
            text=True,
            errors="replace",
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

    def tls_library(self) -> str:
        """Return the version string of the TLS library ATS was built with.

        Test clients such as openssl and Python's ssl module may use a
        different library, e.g. system OpenSSL against a BoringSSL ATS.
        """

        return self._context.runtime.versions.get("openssl_str", "")

    def plugin_exists(self, name: str) -> bool:
        """Return whether an installed ATS plugin is available.

        :param name: Plugin basename, with or without the ``.so`` suffix.
        """

        filename = name if name.endswith(".so") else f"{name}.so"
        return (Path(self._context.runtime.layout["PLUGINDIR"]) / filename).is_file()

    def use_nameserver(self, port: int) -> None:
        """Resolve hostnames through a test DNS server instead of the host's resolver.

        :param port: UDP port of a DNS server listening on 127.0.0.1.
        """

        self._nameserver_port = port

    def define_placeholders(self, values: Mapping[str, object]) -> None:
        """Add owner-defined placeholders, such as an origin's port, to those :meth:`replace_placeholders` expands.

        :param values: Placeholder markers, including their braces, mapped to
            their replacement values.
        """

        self._placeholders.update({marker: str(value) for marker, value in values.items()})

    def replace_placeholders(self, value: Any) -> Any:
        """Expand this instance's ports, directories, and defined placeholders in configuration values.

        :param value: A string, or a list or mapping nested with strings;
            other values are returned unchanged.
        """

        if isinstance(value, Mapping):
            return {key: self.replace_placeholders(item) for key, item in value.items()}
        if isinstance(value, list):
            return [self.replace_placeholders(item) for item in value]
        if not isinstance(value, str):
            return value
        if not self._paths:
            raise RuntimeError(f"{self.name} has no run root until it starts")
        replacements = {
            "{ATS_HTTP_PORT}": str(self.http_port),
            "{ATS_HTTPS_PORT}": str(self.https_port),
            "{ATS_PROXY_PROTOCOL_PORT}": str(self.proxy_protocol_port),
            "{ATS_PROXY_PROTOCOL_HTTPS_PORT}": str(self.proxy_protocol_https_port),
            "{ATS_ROOT}": str(self._paths["root"]),
            "{CONFIG_DIR}": str(self._paths["config"]),
            "{LOG_DIR}": str(self._paths["log"]),
            "{RUNTIME_DIR}": str(self._paths["rpc_runtime"]),
            "{STORAGE_DIR}": str(self._paths["storage"]),
            "{ATS_SSL_DIR}": str(self._paths["ssl"]),
            "{TEST_DIR}": str(self._test_directory),
            "{PYTHON}": sys.executable,
            **self._placeholders,
        }
        for marker, replacement in replacements.items():
            value = value.replace(marker, replacement)
        return value

    def run_commands(self, commands: Sequence[object], phase: str) -> None:
        """Run shell commands in this instance's run root, failing on the first that exits nonzero.

        :param commands: Bash commands, expanded by :meth:`replace_placeholders`.
        :param phase: Lifecycle phase named in failure messages.
        """

        for command in commands:
            rendered = self.replace_placeholders(str(command))
            result = self.run_shell(rendered)
            if result.returncode != 0:
                raise AssertionError(f"ATS {phase} command failed ({rendered}):\n{result.output}")

    def validate_diagnostics(self) -> None:
        """Fail on diagnostics that show Traffic Server misbehaved.

        Once :meth:`expect_start_failure` is called, or a manifest declares
        ``startup_failure``, no diagnostics are checked, including whether
        diags.log exists. Otherwise fatal diagnostics fail the test, as do
        errors not accepted by :meth:`allow_errors` and unrecognized
        configuration values, unless the instance was created with
        ``disable_log_checks=True``.
        """

        if not self._was_started or self._allow_fatal_diagnostics:
            return
        assert self.diags_log.is_file(), f"Missing ATS diagnostic log: {self.diags_log}"
        content = self.diags_log.read_text(errors="replace")
        lines = content.splitlines()
        expressions = ["FATAL:"]
        if not self._process_options.get("disable_log_checks", False):
            expressions += ["ERROR:", "Unrecognized configuration value"]
        for expression in expressions:
            unexpected = [
                line
                for line in lines
                if expression in line
                and not (expression == "ERROR:" and any(allowed.search(line) for allowed in self._allowed_errors))
            ]
            if unexpected:
                raise AssertionError(f"{self.name} emitted an unexpected {expression!r} diagnostic:\n{content}")

    def remove_temporary_directories(self) -> None:
        """Delete the short directories outside the sandbox that hold this instance's Unix sockets."""

        for directory in self._temporary_directories:
            shutil.rmtree(directory, ignore_errors=True)
        self._temporary_directories.clear()
        if self._uds_directory is not None:
            shutil.rmtree(self._uds_directory, ignore_errors=True)
            self._uds_directory = None

    def close(self) -> None:
        """Stop Traffic Server, validate its diagnostics, and remove its temporary directories."""

        try:
            self.stop()
            self.validate_diagnostics()
        finally:
            self.remove_temporary_directories()

    def _launch(self, config: Mapping[str, Any]) -> ManagedProcess:
        """Build the run root from staged configuration, start Traffic Server, and wait until it is ready.

        :param config: Staged configuration in the schema of a manifest's
            ``urtest.ats`` mapping.
        """

        runtime = self._context.runtime
        process_config = dict(config.get("process_config", {}))
        paths = self._prepare_tree()
        enable_tls = bool(process_config.get("enable_tls", False))
        enable_quic = bool(process_config.get("enable_quic", False))
        enable_proxy_protocol = bool(process_config.get("enable_proxy_protocol", False))
        enable_proxy_protocol_cp_src = bool(process_config.get("enable_proxy_protocol_cp_src", False))
        enable_cache = bool(process_config.get("enable_cache", config.get("enable_cache", True)))
        enable_cripts = bool(process_config.get("enable_cripts", False))

        records: dict[str, Any] = {
            "config_update_interval_ms": 20,
            "http": {
                "server_ports": self._server_ports(
                    enable_tls,
                    enable_quic,
                    enable_proxy_protocol,
                    enable_proxy_protocol_cp_src,
                ),
                "wait_for_cache": 1,
            },
        }
        # ATS writes a log buffer when it is max_secs_per_buffer old, checked every
        # periodic_tasks_interval, both 5 s by default. At 1 s each, an entry reaches disk within a
        # few seconds, so a test can read its logs without a long wait.
        records["log"] = {"max_secs_per_buffer": 1, "periodic_tasks_interval": 1}
        if process_config.get("disable_log_rolling", True):
            records["log"]["rolling_enabled"] = 0
        if not enable_cache:
            records["http"]["cache"] = {"http": 0}
            records["http"].pop("wait_for_cache")
        if enable_quic:
            records.setdefault("udp", {})["threads"] = 1
        if self._nameserver_port is not None:
            records["dns"] = {"nameservers": f"127.0.0.1:{self._nameserver_port}", "resolv_conf": "NULL"}

        if enable_tls:
            self._configure_tls(config, records, paths)
        elif config.get("copy_to_ssl_dir"):
            self._copy_ssl_files(config, paths)
        if enable_cripts:
            compiler = paths["bin"] / "cripts_compiler.sh"
            shutil.copy2(runtime.repository_root / "tools" / "cripts" / "compiler.sh", compiler)
            compiler.chmod(0o755)
            records.setdefault("plugin", {})["compiler_path"] = str(compiler)

        configured_records = self.replace_placeholders(config.get("records_config", {}))
        records_path = paths["config"] / "records.yaml"
        write_yaml(records_path, merge_flat_records(configured_records, records))
        for document in config.get("records_documents", []):
            with records_path.open("a") as stream:
                stream.write("---\n")
                yaml.safe_dump(merge_flat_records(document, {}), stream, sort_keys=False)
        self._write_configs(config, paths)

        environment = os.environ.copy()
        environment.update(
            {
                "TS_ROOT": str(self._root),
                "TS_RUNROOT": str(paths["config"] / "runroot.yaml"),
                "PROXY_CONFIG_BIN_PATH": str(paths["bin"]),
                "PROXY_CONFIG_CONFIG_DIR": str(paths["config"]),
                "PROXY_CONFIG_BODY_FACTORY_TEMPLATE_SETS_DIR": str(paths["body_factory"]),
                "PROXY_CONFIG_CACHE_DIR": str(paths["cache"]),
                "PROXY_CONFIG_PLUGIN_PLUGIN_DIR": str(paths["plugin"]),
                "PROXY_CONFIG_LOG_LOGFILE_DIR": str(paths["log"]),
                "PROXY_CONFIG_LOCAL_STATE_DIR": str(paths["rpc_runtime"]),
                "PROXY_CONFIG_SSL_DIR": str(paths["ssl"]),
                "PROXY_CONFIG_STORAGE_DIR": str(paths["storage"]),
                "PROXY_CONFIG_PROCESS_MANAGER_MGMT_PORT": str(self._manager_port),
                "PROXY_CONFIG_ADMIN_SYNTHETIC_PORT": str(self._admin_port),
                "PROXY_CONFIG_ADMIN_AUTOCONF_PORT": str(self._admin_port),
                "PATH": str(paths["bin"]) + os.pathsep + os.environ.get("PATH", ""),
            }
        )
        environment.update(
            {str(name): self.replace_placeholders(str(value)) for name, value in config.get("environment", {}).items()}
        )
        for variable in config.get("unset_environment", []):
            environment.pop(str(variable), None)
        if enable_cripts:
            environment["ATS_ROOT"] = runtime.layout["PREFIX"]
        self._environment = environment
        self.run_commands(config.get("pre_start_commands", []), "pre_start_commands")
        traffic_out = paths["log"] / "traffic.out"
        command: list[str | Path] = [paths["bin"] / "traffic_server"]
        if process_config.get("capture_traffic_out", True):
            command.extend(("--bind_stdout", traffic_out, "--bind_stderr", traffic_out))
        command.extend(str(argument) for argument in process_config.get("server_args", []))
        process = ManagedProcess(
            self.name,
            command,
            self._root,
            environment=environment,
            expected_return_codes=return_codes(config),
        )
        chown_for_ats(self._root)
        process.start()
        # Owned before readiness so that stop() reaps a process that never becomes ready.
        self._process = process
        startup_timeout = float(config.get("startup_timeout", 60 if enable_cripts else 30))
        diags_name = str(config.get("records_config", {}).get("proxy.config.diags.logfile.filename", "diags.log"))
        readiness_paths = (
            (traffic_out, process.stdout_path, process.stderr_path)
            if diags_name in ("stdout", "stderr")
            else (paths["log"] / diags_name,)
        )
        diags_log = readiness_paths[0]
        if expected_failure := config.get("startup_failure"):
            process.wait(timeout=startup_timeout)
            self._exit_was_expected = True
            output = "\n".join(
                path.read_text(errors="replace")
                for path in (diags_log, traffic_out, process.stdout_path, process.stderr_path)
                if path.exists()
            )
            if re.search(str(expected_failure), output) is None:
                raise AssertionError(f"{self.name} did not emit expected startup failure {expected_failure!r}:\n{output}")
            return process
        process.wait_until(
            lambda: (
                any(
                    path.exists() and "NOTE: Traffic Server is fully initialized" in path.read_text(errors="replace")
                    for path in readiness_paths
                )
                or (diags_name in ("stdout", "stderr") and tcp_open(self.http_port))
            ),
            startup_timeout,
            "the fully initialized log message",
        )
        return process

    def _prepare_tree(self) -> dict[str, Path]:
        """Create the run root, linking installed programs and plugins and copying baseline configuration."""

        runtime = self._context.runtime
        root = self._root
        names = ("bin", "config", "body_factory", "plugin", "log", "runtime", "ssl", "storage", "cache")
        paths = {name: root / name for name in names}
        paths["root"] = root
        for path in paths.values():
            path.mkdir(parents=True, exist_ok=True)
        rpc_runtime = Path(tempfile.mkdtemp(prefix="ats-pytest-", dir="/tmp"))
        self._temporary_directories.append(rpc_runtime)
        paths["rpc_runtime"] = rpc_runtime
        _link_directory(Path(runtime.layout["BINDIR"]), paths["bin"])
        _link_directory(Path(runtime.layout["PLUGINDIR"]), paths["plugin"])
        body_factory = Path(runtime.layout["SYSCONFDIR"]) / "body_factory"
        if body_factory.is_dir():
            shutil.copytree(body_factory, paths["body_factory"], dirs_exist_ok=True)
        min_config = runtime.repository_root / "tests" / "tools" / "uranium" / "min_cfg"
        for source in min_config.iterdir():
            if source.is_file() and source.name != "readme.txt":
                shutil.copy2(source, paths["config"] / source.name)
        write_yaml(
            paths["config"] / "runroot.yaml",
            {
                # Keep the JSON-RPC Unix socket below sockaddr_un.sun_path's
                # small platform limit, even under a deeply nested CI sandbox.
                "runtimedir": str(paths["rpc_runtime"]),
                "cachedir": str(paths["cache"]),
                "localstatedir": str(paths["runtime"]),
                "bindir": str(paths["bin"]),
                "prefix": str(root),
                "logdir": str(paths["log"]),
                "sysconfdir": str(paths["config"]),
            },
        )
        chown_for_ats(paths["rpc_runtime"])
        self._paths = paths
        return paths

    def _server_ports(
        self,
        enable_tls: bool,
        enable_quic: bool,
        enable_proxy_protocol: bool,
        enable_proxy_protocol_cp_src: bool,
    ) -> str:
        """Return the default proxy.config.http.server_ports value for the enabled listeners.

        :param enable_tls: Whether to listen for TLS on the HTTPS port.
        :param enable_quic: Whether to listen for QUIC on the HTTPS port.
        :param enable_proxy_protocol: Whether to add PROXY protocol listeners.
        :param enable_proxy_protocol_cp_src: Whether those listeners take the
            client address from the PROXY header.
        """

        value = str(self.http_port)
        if enable_tls:
            value += f" {self.https_port}:ssl"
        if enable_quic:
            value += f" {self.https_port}:quic"
        if enable_proxy_protocol:
            client_flag = ":pp-clnt" if enable_proxy_protocol_cp_src else ""
            value += f" {self.proxy_protocol_port}:pp{client_flag}"
            if enable_tls:
                value += f" {self.proxy_protocol_https_port}:ssl:pp{client_flag}"
        return value

    def _configure_tls(self, config: Mapping[str, Any], records: dict[str, Any], paths: dict[str, Path]) -> None:
        """Install inbound TLS material and point the TLS records at it.

        :param config: Staged configuration in the ``urtest.ats`` schema.
        :param records: Default records, updated with the TLS certificate paths.
        :param paths: Run-root directories by role.
        """

        ssl_multicert = config.get("ssl_multicert_yaml")
        if ssl_multicert is None:
            for filename in ("server.pem", "server.key"):
                shutil.copy2(self._context.runtime.test_tools / "ssl" / filename, paths["ssl"] / filename)
            ssl_multicert = [
                "ssl_multicert:",
                "  - ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
                '    dest_ip: "*"',
            ]
        records.setdefault("ssl", {}).setdefault("server", {})["cert"] = {"path": str(paths["ssl"])}
        records["ssl"]["server"]["private_key"] = {"path": str(paths["ssl"])}
        self._copy_ssl_files(config, paths)
        ssl_multicert_path = paths["config"] / "ssl_multicert.yaml"
        if isinstance(ssl_multicert, Mapping):
            write_yaml(ssl_multicert_path, ssl_multicert)
        else:
            ssl_multicert_path.write_text("\n".join(str(line) for line in ssl_multicert) + "\n")
        if "sni_yaml" in config:
            write_yaml(paths["config"] / "sni.yaml", self.replace_placeholders(config["sni_yaml"]))

    def _copy_ssl_files(self, config: Mapping[str, Any], paths: dict[str, Path]) -> None:
        """Copy test-owned TLS material for inbound or outbound TLS.

        :param config: Staged configuration in the ``urtest.ats`` schema.
        :param paths: Run-root directories by role.
        """

        for value in config.get("copy_to_ssl_dir", []):
            source = (self._test_directory / str(value)).resolve()
            if not source.is_file():
                raise ReplayConfigError(f"TLS file does not exist: {source}")
            shutil.copy2(source, paths["ssl"] / source.name)

    def _write_configs(self, config: Mapping[str, Any], paths: dict[str, Path]) -> None:
        """Write plugin, remap, and the other staged configuration files into the run root.

        :param config: Staged configuration in the ``urtest.ats`` schema.
        :param paths: Run-root directories by role.
        """

        config_dir = paths["config"]
        plugin_lines = [format_plugin_entry(self.replace_placeholders(entry)) for entry in config.get("plugin_config", [])]
        (config_dir / "plugin.config").write_text("\n".join(plugin_lines) + ("\n" if plugin_lines else ""))
        for plugin_value in config.get("copy_custom_plugin", []):
            source = self._context.runtime.resolve_artifact(self._test_directory, str(plugin_value))
            if not source.is_file():
                raise ReplayConfigError(f"Custom plugin does not exist: {source}")
            destination = paths["plugin"] / source.name
            if destination.exists() or destination.is_symlink():
                destination.unlink()
            shutil.copy2(source, destination)

        for key, filename in (
            ("parent_config", "parent.config"),
            ("cache_config", "cache.config"),
            ("hosting_config", "hosting.config"),
            ("splitdns_config", "splitdns.config"),
        ):
            lines = [str(self.replace_placeholders(line)) for line in config.get(key, [])]
            (config_dir / filename).write_text("\n".join(lines) + ("\n" if lines else ""))
        if "logging_yaml" in config:
            write_yaml(config_dir / "logging.yaml", config["logging_yaml"])
        if "storage_yaml" in config:
            write_yaml(config_dir / "storage.yaml", config["storage_yaml"])
        if "storage_lines" in config:
            (config_dir / "storage.yaml").write_text("\n".join(config["storage_lines"]) + "\n")
        if "ssl_multicert_lines" in config:
            (config_dir / "ssl_multicert.yaml").write_text("\n".join(config["ssl_multicert_lines"]) + "\n")
        if "ip_allow_yaml" in config:
            write_yaml(config_dir / "ip_allow.yaml", config["ip_allow_yaml"])
        if "ip_allow_lines" in config:
            (config_dir / "ip_allow.yaml").write_text("\n".join(config["ip_allow_lines"]) + "\n")
        if "remap_yaml" in config:
            write_yaml(config_dir / "remap.yaml", config["remap_yaml"])
        if "remap_yaml_lines" in config:
            lines = [str(self.replace_placeholders(line)) for line in config["remap_yaml_lines"]]
            (config_dir / "remap.yaml").write_text("\n".join(lines) + "\n")

        remap_lines = []
        for entry in config.get("remap_config", []):
            if isinstance(entry, str):
                remap_lines.append(str(self.replace_placeholders(entry)))
                continue
            line = f"map {self.replace_placeholders(entry['from'])} {self.replace_placeholders(entry['to'])}"
            for plugin in entry.get("plugins", []):
                line += f" @plugin={plugin['name']}"
                for argument in plugin.get("args", []):
                    line += f" @pparam={self.replace_placeholders(argument)}"
            for option in entry.get("options", []):
                line += f" {self.replace_placeholders(option)}"
            remap_lines.append(line)
        (config_dir / "remap.config").write_text("\n".join(remap_lines) + ("\n" if remap_lines else ""))

        for value in config.get("copy_to_config_dir", []):
            if isinstance(value, Mapping):
                source_value = str(value["source"])
                destination_value = str(value.get("destination", Path(source_value).name))
            else:
                source_value = destination_value = str(value)
            source = (self._test_directory / source_value).resolve()
            destination = config_dir / destination_value
            if source.is_dir():
                shutil.copytree(source, destination, dirs_exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        for value in config.get("template_to_config_dir", []):
            source = (self._test_directory / str(value)).resolve()
            if not source.is_file():
                raise ReplayConfigError(f"Configuration template does not exist: {source}")
            destination = config_dir / Path(str(value)).name
            rendered = self.replace_placeholders(source.read_text())
            destination.write_text(rendered)
        for filename in config.get("omit_config_files", []):
            (config_dir / str(filename)).unlink(missing_ok=True)
        for filename, content in config.get("inline_config_files", {}).items():
            destination = config_dir / str(filename)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(str(self.replace_placeholders(content)))
        for filename, content in config.get("inline_runtime_files", {}).items():
            destination = paths["runtime"] / str(filename)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(str(self.replace_placeholders(content)))
        for relative_path, content in config.get("body_factory_files", {}).items():
            destination = paths["body_factory"] / str(relative_path)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(str(self.replace_placeholders(content)))


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

    def tls_library(self) -> str:
        """Return the version string of the TLS library ATS was built with.

        Test clients such as openssl and Python's ssl module may use a
        different library, e.g. system OpenSSL against a BoringSSL ATS.
        """

        return self._context.runtime.versions.get("openssl_str", "")

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


def chown_for_ats(root: Path) -> None:
    """Give @a root's tree to the unprivileged user Traffic Server drops to when tests run as root.

    :param root: Directory whose contents Traffic Server must read and write.
    """

    if os.geteuid() != 0:
        return
    try:
        uid = pwd.getpwnam("nobody").pw_uid
        try:
            gid = grp.getgrnam("nogroup").gr_gid
        except KeyError:
            gid = grp.getgrnam("nobody").gr_gid
    except KeyError:
        return
    for directory, directories, files in os.walk(root):
        os.chown(directory, uid, gid)
        for name in [*directories, *files]:
            path = Path(directory) / name
            if not path.is_symlink():
                os.chown(path, uid, gid)


def _link_directory(source: Path, destination: Path) -> None:
    """Symlink each entry of an installed directory into the run root, keeping entries already there.

    :param source: Installed directory, such as the ATS bin or plugin directory.
    :param destination: Run-root directory receiving the links.
    """

    for entry in source.iterdir():
        target = destination / entry.name
        if not target.exists():
            target.symlink_to(entry, target_is_directory=entry.is_dir())
