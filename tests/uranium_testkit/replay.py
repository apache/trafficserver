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
"""Execute Proxy Verifier replay files as isolated ATS system tests."""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import Any
import difflib
import grp
import json
import os
import pwd
import re
import shlex
import shutil
import socket
import subprocess
import tempfile
import time

from dnslib import DNSRecord

from .config import ReplayConfigError, ReplaySpec, format_plugin_entry, merge_flat_records, replace_server_ports, write_yaml
from .process import ManagedProcess
from .runtime import TestRuntime


class ReplaySkip(RuntimeError):
    """Signal that runtime capabilities do not satisfy replay requirements."""


class ReplayTest:
    """Run one replay file as one independently reportable test item."""

    def __init__(self, spec: ReplaySpec, runtime: TestRuntime, node_name: str | None = None) -> None:
        self.spec = spec
        self.runtime = runtime
        self.node_name = node_name or spec.path.stem
        self.sandbox = runtime.item_sandbox(spec.path, self.node_name)
        self.processes: list[ManagedProcess] = []
        self._temporary_directories: list[Path] = []
        self.http_port = runtime.allocate_port()
        self.https_port = runtime.allocate_port()
        self.server_http_port = runtime.allocate_port()
        self.server_https_port = runtime.allocate_port()
        self.manager_port = runtime.allocate_port()
        self.admin_port = runtime.allocate_port()
        self.dns_port: int | None = None
        self.ats_environment: dict[str, str] | None = None
        self.ats_paths: dict[str, Path] = {}

    @property
    def test_directory(self) -> Path:
        """Return the legacy test directory associated with this replay."""

        parent = self.spec.path.parent
        return parent.parent if parent.name in ("replay", "replays") else parent

    def run(self) -> None:
        """Execute the replay and validate all declared outputs."""

        reason = self.runtime.requirement_failure(self.spec.urtest.get("requires", {}))
        if reason:
            raise ReplaySkip(reason)
        self.runtime.prepare_sandbox(self.sandbox)

        server = None
        ats = None
        client = None
        try:
            self._start_dns()
            server = self._start_verifier_server()
            ats = self._start_ats()
            client = self._run_verifier_client()
            self._validate_background_processes(server, ats)
            self._check_metrics()
        finally:
            for process in reversed(self.processes):
                process.stop()
            for directory in self._temporary_directories:
                shutil.rmtree(directory, ignore_errors=True)

        if client is not None:
            self._validate_process_output(client, self.spec.urtest["client"], "Violation|Invalid status")
        if server is not None:
            self._validate_process_output(server, self.spec.urtest["server"], "Violation")
        if ats is not None:
            self._validate_ats_logs()

    def _start_dns(self) -> ManagedProcess | None:
        config = self.spec.urtest.get("dns")
        if config is None:
            return None
        self.dns_port = self.runtime.allocate_port(socket.SOCK_DGRAM)
        name = str(config.get("name", "dns"))
        directory = self.sandbox / name
        directory.mkdir()
        default = config.get("process_config", {}).get("default", "127.0.0.1")
        otherwise = [default] if isinstance(default, str) else default
        mappings = []
        for hostname, addresses in config.get("records", {}).items():
            mappings.append({hostname if hostname.endswith(".") else hostname + ".": addresses})
        zone_file = directory / "dns_file.json"
        zone_file.write_text(json.dumps({"mappings": mappings, "otherwise": otherwise}))
        process = ManagedProcess(name, ["microdns", "INADDR_LOOPBACK", str(self.dns_port), str(zone_file)], directory)
        process.start()
        self.processes.append(process)

        def responds() -> bool:
            try:
                query = DNSRecord.question("pytest-readiness.invalid")
                query.send("127.0.0.1", self.dns_port or 0, timeout=0.1)
                return True
            except (OSError, socket.timeout):
                return False

        process.wait_until(responds, 10, f"DNS replies on 127.0.0.1:{self.dns_port}")
        return process

    def _start_verifier_server(self) -> ManagedProcess:
        config = self.spec.urtest["server"]
        process_config = dict(config.get("process_config", {}))
        name = str(config.get("name", "server"))
        directory = self.sandbox / name
        directory.mkdir()
        ssl_dir = self.runtime.test_tools / "proxy-verifier" / "ssl"
        command = [
            self.runtime.verifier_bin / "verifier-server",
            "run",
            "--listen-http",
            f"127.0.0.1:{self.server_http_port}",
            "--listen-https",
            f"127.0.0.1:{self.server_https_port}",
            "--server-cert",
            ssl_dir / "server.pem",
            "--ca-certs",
            ssl_dir / "ca.pem",
            "--tls-secrets-log-file",
            directory / "tls_secrets.txt",
            self.spec.path,
        ]
        if process_config.get("verbose", True):
            command.extend(["--verbose", "diag"])
        command.extend(shlex.split(str(process_config.get("other_args", ""))))
        process = ManagedProcess(
            name,
            command,
            directory,
            expected_return_codes=self._return_codes(config),
        )
        process.start()
        self.processes.append(process)
        process.wait_until(
            lambda: self._tcp_open(self.server_http_port),
            10,
            f"HTTP listener on 127.0.0.1:{self.server_http_port}",
        )
        return process

    def _start_ats(self) -> ManagedProcess:
        config = self.spec.urtest["ats"]
        process_config = dict(config.get("process_config", {}))
        name = str(config.get("name", "ts"))
        ts_root = self.sandbox / name
        paths = self._prepare_ats_tree(ts_root)
        self.ats_paths = paths
        enable_tls = bool(process_config.get("enable_tls", False))
        enable_quic = bool(process_config.get("enable_quic", False))
        enable_cache = bool(process_config.get("enable_cache", config.get("enable_cache", True)))
        enable_cripts = bool(process_config.get("enable_cripts", False))

        records: dict[str, Any] = {
            "config_update_interval_ms": 20,
            "http": {
                "server_ports": self._server_ports(enable_tls, enable_quic),
                "wait_for_cache": 1,
            },
        }
        if not enable_cache:
            records["http"]["cache"] = {"http": 0}
            records["http"].pop("wait_for_cache")
        if enable_quic:
            records.setdefault("udp", {})["threads"] = 1
        if self.dns_port is not None:
            records["dns"] = {"nameservers": f"127.0.0.1:{self.dns_port}", "resolv_conf": "NULL"}

        if enable_tls:
            self._configure_tls(config, records, paths)
        if enable_cripts:
            compiler = paths["bin"] / "cripts_compiler.sh"
            shutil.copy2(self.runtime.repository_root / "tools" / "cripts" / "compiler.sh", compiler)
            compiler.chmod(0o755)
            records.setdefault("plugin", {})["compiler_path"] = str(compiler)

        configured_records = config.get("records_config", {})
        write_yaml(paths["config"] / "records.yaml", merge_flat_records(configured_records, records))
        self._write_ats_configs(config, paths)

        environment = os.environ.copy()
        environment.update(
            {
                "TS_ROOT": str(ts_root),
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
                "PROXY_CONFIG_PROCESS_MANAGER_MGMT_PORT": str(self.manager_port),
                "PROXY_CONFIG_ADMIN_SYNTHETIC_PORT": str(self.admin_port),
                "PROXY_CONFIG_ADMIN_AUTOCONF_PORT": str(self.admin_port),
                "PATH": str(paths["bin"]) + os.pathsep + os.environ.get("PATH", ""),
            })
        if enable_cripts:
            environment["ATS_ROOT"] = self.runtime.layout["PREFIX"]
        self.ats_environment = environment
        traffic_out = paths["log"] / "traffic.out"
        process = ManagedProcess(
            name,
            [paths["bin"] / "traffic_server", "--bind_stdout", traffic_out, "--bind_stderr", traffic_out],
            ts_root,
            environment=environment,
        )
        self._chown_for_ats(ts_root)
        process.start()
        self.processes.append(process)
        startup_timeout = float(config.get("startup_timeout", 60 if enable_cripts else 10))
        diags_log = paths["log"] / "diags.log"
        process.wait_until(
            lambda: diags_log.exists() and "NOTE: Traffic Server is fully initialized" in diags_log.read_text(errors="replace"),
            startup_timeout,
            "the fully initialized log message",
        )
        return process

    def _run_verifier_client(self) -> ManagedProcess:
        config = self.spec.urtest["client"]
        process_config = dict(config.get("process_config", {}))
        name = str(config.get("name", "client"))
        directory = self.sandbox / name
        directory.mkdir()
        ats_process_config = self.spec.urtest["ats"].get("process_config", {})
        enable_tls = bool(ats_process_config.get("enable_tls", False))
        enable_quic = bool(ats_process_config.get("enable_quic", False))
        ssl_dir = self.runtime.test_tools / "proxy-verifier" / "ssl"
        http_ports = process_config.pop("http_ports", [self.http_port])
        https_ports = process_config.pop("https_ports", [self.https_port] if enable_tls else [])
        http3_ports = process_config.pop("http3_ports", [self.https_port] if enable_quic else [])
        command: list[str | Path] = [self.runtime.verifier_bin / "verifier-client", "run", self.spec.path]
        if http_ports:
            command.extend(["--connect-http", self._address_argument(http_ports)])
        if https_ports:
            command.extend(["--connect-https", self._address_argument(https_ports)])
        if http3_ports:
            qlog = directory / "qlog_directory"
            qlog.mkdir()
            command.extend(["--connect-http3", self._address_argument(http3_ports), "--qlog-dir", qlog])
        if https_ports or http3_ports:
            command.extend(
                [
                    "--client-cert",
                    ssl_dir / "client.pem",
                    "--ca-certs",
                    ssl_dir / "ca.pem",
                    "--tls-secrets-log-file",
                    directory / "tls_secrets.txt",
                ])
        if process_config.get("verbose", True):
            command.extend(["--verbose", "diag"])
        other_args = str(process_config.get("other_args", ""))
        command.extend(shlex.split(other_args))
        poll_timeout = process_config.get("poll_timeout")
        if poll_timeout is not None:
            command.extend(["--poll-timeout", str(poll_timeout)])
        if "thread-limit" not in other_args and not process_config.get("run_parallel", False):
            command.extend(["--thread-limit", "1"])
        process = ManagedProcess(
            name,
            command,
            directory,
            expected_return_codes=self._return_codes(config),
        )
        process.start()
        self.processes.append(process)
        process.wait(timeout=float(config.get("timeout", 60)))
        return process

    def _prepare_ats_tree(self, root: Path) -> dict[str, Path]:
        names = ("bin", "config", "body_factory", "plugin", "log", "runtime", "ssl", "storage", "cache")
        paths = {name: root / name for name in names}
        for path in paths.values():
            path.mkdir(parents=True, exist_ok=True)
        rpc_runtime = Path(tempfile.mkdtemp(prefix="ats-pytest-", dir="/tmp"))
        self._temporary_directories.append(rpc_runtime)
        paths["rpc_runtime"] = rpc_runtime
        self._link_directory(Path(self.runtime.layout["BINDIR"]), paths["bin"])
        self._link_directory(Path(self.runtime.layout["PLUGINDIR"]), paths["plugin"])
        body_factory = Path(self.runtime.layout["SYSCONFDIR"]) / "body_factory"
        if body_factory.is_dir():
            shutil.copytree(body_factory, paths["body_factory"], dirs_exist_ok=True)
        min_config = self.runtime.repository_root / "tests" / "uranium_testkit" / "min_cfg"
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
        self._chown_for_ats(paths["rpc_runtime"])
        return paths

    def _configure_tls(self, config: Mapping[str, Any], records: dict[str, Any], paths: dict[str, Path]) -> None:
        ssl_multicert = config.get("ssl_multicert_yaml")
        if ssl_multicert is None:
            for filename in ("server.pem", "server.key"):
                shutil.copy2(self.runtime.test_tools / "ssl" / filename, paths["ssl"] / filename)
            ssl_multicert = [
                "ssl_multicert:",
                "  - ssl_cert_name: server.pem",
                "    ssl_key_name: server.key",
                '    dest_ip: "*"',
            ]
            records.setdefault("ssl", {}).setdefault("server", {})["cert"] = {"path": str(paths["ssl"])}
            records["ssl"]["server"]["private_key"] = {"path": str(paths["ssl"])}
        (paths["config"] / "ssl_multicert.yaml").write_text("\n".join(ssl_multicert) + "\n")
        if "sni_yaml" in config:
            write_yaml(paths["config"] / "sni.yaml", config["sni_yaml"])

    def _write_ats_configs(self, config: Mapping[str, Any], paths: dict[str, Path]) -> None:
        config_dir = paths["config"]
        plugin_lines = [format_plugin_entry(entry) for entry in config.get("plugin_config", [])]
        (config_dir / "plugin.config").write_text("\n".join(plugin_lines) + ("\n" if plugin_lines else ""))
        for plugin_value in config.get("copy_custom_plugin", []):
            source = self.runtime.resolve_artifact(self.test_directory, str(plugin_value))
            if not source.is_file():
                raise ReplayConfigError(f"Custom plugin does not exist: {source}")
            destination = paths["plugin"] / source.name
            if destination.exists() or destination.is_symlink():
                destination.unlink()
            shutil.copy2(source, destination)

        replacements = (self.server_http_port, self.server_https_port)
        for key, filename in (("parent_config", "parent.config"), ("cache_config", "cache.config")):
            lines = [replace_server_ports(str(line), *replacements) for line in config.get(key, [])]
            (config_dir / filename).write_text("\n".join(lines) + ("\n" if lines else ""))
        if "logging_yaml" in config:
            write_yaml(config_dir / "logging.yaml", config["logging_yaml"])
        if "storage_yaml" in config:
            write_yaml(config_dir / "storage.yaml", config["storage_yaml"])

        remap_lines = []
        for entry in config.get("remap_config", []):
            if isinstance(entry, str):
                remap_lines.append(replace_server_ports(entry, *replacements))
                continue
            line = f"map {entry['from']} {replace_server_ports(str(entry['to']), *replacements)}"
            for plugin in entry.get("plugins", []):
                line += f" @plugin={plugin['name']}"
                for argument in plugin.get("args", []):
                    line += f" @pparam={argument}"
            for option in entry.get("options", []):
                line += f" {option}"
            remap_lines.append(line)
        (config_dir / "remap.config").write_text("\n".join(remap_lines) + ("\n" if remap_lines else ""))

        for value in config.get("copy_to_config_dir", []):
            source = (self.test_directory / value).resolve()
            destination = config_dir / value
            if source.is_dir():
                shutil.copytree(source, destination, dirs_exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)

    def _check_metrics(self) -> None:
        checks = self.spec.urtest["ats"].get("metric_checks", [])
        for check in checks:
            time.sleep(float(check.get("delay", 2)))
            metric = str(check["metric"])
            command = [self.ats_paths["bin"] / "traffic_ctl", "metric", "get", metric]
            result = subprocess.run(command, env=self.ats_environment, capture_output=True, text=True, timeout=10)
            if result.returncode != 0:
                raise AssertionError(f"Could not read metric {metric}:\n{result.stdout}{result.stderr}")
            fields = result.stdout.split()
            if len(fields) < 2:
                raise AssertionError(f"Metric {metric} was absent: {result.stdout!r}")
            actual_text = fields[-1]
            if "value" in check and re.fullmatch(str(check["value"]), actual_text) is None:
                raise AssertionError(f"Metric {metric} was {actual_text}, expected {check['value']}")
            if "min" in check and float(actual_text) < float(check["min"]):
                raise AssertionError(f"Metric {metric} was {actual_text}, expected at least {check['min']}")
            if "value" not in check and "min" not in check:
                raise ReplayConfigError(f"metric_checks entry for {metric} must specify value or min")

    def _validate_background_processes(self, server: ManagedProcess, ats: ManagedProcess) -> None:
        """Detect a server or unit-under-test exit before intentional teardown."""

        if ats.return_code is not None:
            raise AssertionError(f"ATS exited unexpectedly with status {ats.return_code}.\n{ats.output()}")
        if server.return_code is not None and server.return_code not in server.expected_return_codes:
            raise AssertionError(
                f"Proxy Verifier server exited with status {server.return_code}; expected "
                f"{sorted(server.expected_return_codes)}.\n{server.output()}")

    def _validate_process_output(self, process: ManagedProcess, config: Mapping[str, Any], default_excludes: str) -> None:
        output = ""
        for path in (process.stdout_path, process.stderr_path):
            if path.exists():
                output += path.read_text(errors="replace")
        if re.search(default_excludes, output):
            raise AssertionError(f"Unexpected Proxy Verifier diagnostic in {process.name}:\n{output}")
        self._validate_text(output, config.get("log_validation", {}), process.name)

    def _validate_ats_logs(self) -> None:
        config = self.spec.urtest["ats"]
        if not config.get("process_config", {}).get("disable_log_checks", False):
            diags = (self.ats_paths["log"] / "diags.log").read_text(errors="replace")
            for expression in ("ERROR:", "FATAL:", "Unrecognized configuration value"):
                if expression in diags:
                    raise AssertionError(f"Unexpected {expression!r} in diags.log:\n{diags}")

        validation = config.get("log_validation", {})
        path_by_key = {
            "traffic_out": self.ats_paths["log"] / "traffic.out",
            "diags_log": self.ats_paths["log"] / "diags.log",
            "error_log": self.ats_paths["log"] / "error.log",
        }
        for key, path in path_by_key.items():
            rules = validation.get(key, {})
            if rules:
                self._validate_text(path.read_text(errors="replace") if path.exists() else "", rules, key)
        access = validation.get("access_log", {})
        if access:
            self._validate_gold(self.ats_paths["log"] / access["filename"], self.test_directory / access["gold_file"])

    def _validate_text(self, content: str, rules: Mapping[str, Any], label: str) -> None:
        flags = re.MULTILINE
        for entry in rules.get("contains", []):
            entry_flags = flags | (re.DOTALL if entry.get("multiline", False) else 0)
            if re.search(str(entry["expression"]), content, entry_flags) is None:
                description = entry.get("description", f"{label} should contain {entry['expression']!r}")
                raise AssertionError(f"{description}\n--- {label} ---\n{content}")
        for entry in rules.get("excludes", []):
            if re.search(str(entry["expression"]), content, flags) is not None:
                description = entry.get("description", f"{label} should exclude {entry['expression']!r}")
                raise AssertionError(f"{description}\n--- {label} ---\n{content}")
        if "gold_file" in rules:
            actual_path = self.sandbox / f"{label}.actual"
            actual_path.write_text(content)
            self._validate_gold(actual_path, self.test_directory / rules["gold_file"])

    @staticmethod
    def _validate_gold(actual_path: Path, expected_path: Path) -> None:
        actual = (actual_path.read_text(errors="replace") if actual_path.exists() else "").replace("\r\n", "\n")
        expected = expected_path.read_text(errors="replace").replace("\r\n", "\n")
        pattern = "\\A" + ".*?".join(re.escape(part) for part in re.split(r"(?:\{\}|``)", expected)) + "\\Z"
        if re.match(pattern, actual, re.DOTALL) is None:
            difference = "".join(
                difflib.unified_diff(expected.splitlines(True), actual.splitlines(True), str(expected_path), str(actual_path)))
            raise AssertionError(f"Output did not match gold file:\n{difference}")

    def _server_ports(self, enable_tls: bool, enable_quic: bool) -> str:
        value = str(self.http_port)
        if enable_tls:
            value += f" {self.https_port}:ssl"
        if enable_quic:
            value += f" {self.https_port}:quic"
        return value

    @staticmethod
    def _return_codes(config: Mapping[str, Any]) -> Iterable[int]:
        codes = config.get("return_code", 0)
        return codes if isinstance(codes, list) else [codes]

    @staticmethod
    def _address_argument(ports: Iterable[int]) -> str:
        return ",".join(f"127.0.0.1:{port}" for port in ports)

    @staticmethod
    def _tcp_open(port: int) -> bool:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return True
        except OSError:
            return False

    @staticmethod
    def _link_directory(source: Path, destination: Path) -> None:
        for entry in source.iterdir():
            target = destination / entry.name
            if not target.exists():
                target.symlink_to(entry, target_is_directory=entry.is_dir())

    @staticmethod
    def _chown_for_ats(root: Path) -> None:
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
