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

from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any
import glob
import grp
import json
import os
import pwd
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from dnslib import DNSRecord
import yaml

from .assertions import assert_matches_gold
from .config import ReplayConfigError, ReplaySpec, format_plugin_entry, merge_flat_records, write_yaml
from .process import ManagedProcess
from .runtime import TestRuntime
from .utils import loopback_addresses, tcp_open


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
        self.proxy_protocol_port = runtime.allocate_port()
        self.proxy_protocol_https_port = runtime.allocate_port()
        self.server_http_port = runtime.allocate_port()
        self.server_extra_http_port = runtime.allocate_port()
        self.server_https_port = runtime.allocate_port()
        self.manager_port = runtime.allocate_port()
        self.admin_port = runtime.allocate_port()
        self.unused_port = runtime.allocate_port()
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

        if reason := self.spec.urtest.get("skip"):
            raise ReplaySkip(str(reason))
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
            self._run_pre_client_commands()
            client = self._run_verifier_client()
            self._run_post_client_commands()
            self._validate_background_processes(server, ats)
            self._check_metrics()
            self._check_files()
        finally:
            for process in reversed(self.processes):
                process.stop()
            for directory in self._temporary_directories:
                shutil.rmtree(directory, ignore_errors=True)

        if client is not None:
            client_excludes = "" if self.spec.urtest["client"].get("process_config", {}).get("allow_errors") else \
                "Violation|Invalid status"
            self._validate_process_output(client, self.spec.urtest["client"], client_excludes)
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

    def _start_verifier_server(self) -> ManagedProcess | None:
        config = self.spec.urtest["server"]
        if not config.get("enabled", True):
            return None
        process_config = dict(config.get("process_config", {}))
        name = str(config.get("name", "server"))
        directory = self.sandbox / name
        directory.mkdir()
        ssl_dir = self.runtime.test_tools / "proxy-verifier" / "ssl"
        server_cert = self._resolve_test_path(process_config.get("ssl_cert"), ssl_dir / "server.pem")
        ca_certs = self._resolve_test_path(process_config.get("ca_cert"), ssl_dir / "ca.pem")
        http_ports = [self.server_http_port]
        if process_config.get("listen_extra_http", False):
            http_ports.append(self.server_extra_http_port)
        command = [
            self.runtime.verifier_bin / "verifier-server",
            "run",
            "--listen-http",
            loopback_addresses(http_ports),
            "--listen-https",
            f"127.0.0.1:{self.server_https_port}",
            "--server-cert",
            server_cert,
            "--ca-certs",
            ca_certs,
            "--tls-secrets-log-file",
            directory / "tls_secrets.txt",
            self._process_replay_path(config),
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
            lambda: all(tcp_open(port) for port in http_ports),
            10,
            f"HTTP listeners on {loopback_addresses(http_ports)}",
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
        enable_proxy_protocol = bool(process_config.get("enable_proxy_protocol", False))
        enable_proxy_protocol_cp_src = bool(process_config.get("enable_proxy_protocol_cp_src", False))
        enable_cache = bool(process_config.get("enable_cache", config.get("enable_cache", True)))
        enable_cripts = bool(process_config.get("enable_cripts", False))

        records: dict[str, Any] = {
            "config_update_interval_ms": 20,
            "http":
                {
                    "server_ports":
                        self._server_ports(
                            enable_tls,
                            enable_quic,
                            enable_proxy_protocol,
                            enable_proxy_protocol_cp_src,
                        ),
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
        elif config.get("copy_to_ssl_dir"):
            self._copy_ssl_files(config, paths)
        if enable_cripts:
            compiler = paths["bin"] / "cripts_compiler.sh"
            shutil.copy2(self.runtime.repository_root / "tools" / "cripts" / "compiler.sh", compiler)
            compiler.chmod(0o755)
            records.setdefault("plugin", {})["compiler_path"] = str(compiler)

        configured_records = self._replace_runtime_placeholders(config.get("records_config", {}), paths)
        records_path = paths["config"] / "records.yaml"
        write_yaml(records_path, merge_flat_records(configured_records, records))
        for document in config.get("records_documents", []):
            with records_path.open("a") as stream:
                stream.write("---\n")
                yaml.safe_dump(merge_flat_records(document, {}), stream, sort_keys=False)
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
                "PV_HTTP_PORT": str(self.server_http_port),
                "PV_HTTPS_PORT": str(self.server_https_port),
                "PATH": str(paths["bin"]) + os.pathsep + os.environ.get("PATH", ""),
            })
        environment.update(
            {
                str(name): self._replace_runtime_placeholders(str(value), paths)
                for name, value in config.get("environment", {}).items()
            })
        for variable in config.get("unset_environment", []):
            environment.pop(str(variable), None)
        if enable_cripts:
            environment["ATS_ROOT"] = self.runtime.layout["PREFIX"]
        self.ats_environment = environment
        self._run_commands(config.get("pre_start_commands", []), "pre_start_commands", paths, environment)
        traffic_out = paths["log"] / "traffic.out"
        command: list[str | Path] = [paths["bin"] / "traffic_server"]
        if process_config.get("capture_traffic_out", True):
            command.extend(("--bind_stdout", traffic_out, "--bind_stderr", traffic_out))
        command.extend(str(argument) for argument in process_config.get("server_args", []))
        process = ManagedProcess(
            name,
            command,
            ts_root,
            environment=environment,
            expected_return_codes=self._return_codes(config),
        )
        self._chown_for_ats(ts_root)
        process.start()
        self.processes.append(process)
        startup_timeout = float(config.get("startup_timeout", 60 if enable_cripts else 30))
        diags_name = str(config.get("records_config", {}).get("proxy.config.diags.logfile.filename", "diags.log"))
        readiness_paths = (
            (traffic_out, process.stdout_path, process.stderr_path) if diags_name in ("stdout", "stderr") else
            (paths["log"] / diags_name,))
        diags_log = readiness_paths[0]
        if expected_failure := config.get("startup_failure"):
            process.wait(timeout=startup_timeout)
            output = "\n".join(
                path.read_text(errors="replace")
                for path in (diags_log, traffic_out, process.stdout_path, process.stderr_path)
                if path.exists())
            if re.search(str(expected_failure), output) is None:
                raise AssertionError(f"{name} did not emit expected startup failure {expected_failure!r}:\n{output}")
            return process
        process.wait_until(
            lambda: (
                any(
                    path.exists() and "NOTE: Traffic Server is fully initialized" in path.read_text(errors="replace")
                    for path in readiness_paths) or (diags_name in ("stdout", "stderr") and tcp_open(self.http_port))),
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
        command: list[str | Path] = [self.runtime.verifier_bin / "verifier-client", "run", self._process_replay_path(config)]
        if http_ports:
            command.extend(["--connect-http", self._replace_port_placeholders(loopback_addresses(http_ports))])
        if https_ports:
            command.extend(["--connect-https", self._replace_port_placeholders(loopback_addresses(https_ports))])
        if http3_ports:
            qlog = directory / "qlog_directory"
            qlog.mkdir()
            command.extend(
                ["--connect-http3",
                 self._replace_port_placeholders(loopback_addresses(http3_ports)), "--qlog-dir", qlog])
        if https_ports or http3_ports:
            client_cert = self._resolve_test_path(process_config.get("ssl_cert"), ssl_dir / "client.pem")
            ca_certs = self._resolve_test_path(process_config.get("ca_cert"), ssl_dir / "ca.pem")
            command.extend(
                [
                    "--client-cert",
                    client_cert,
                    "--ca-certs",
                    ca_certs,
                    "--tls-secrets-log-file",
                    directory / "tls_secrets.txt",
                ])
        if process_config.get("verbose", True):
            command.extend(["--verbose", "diag"])
        other_args = str(process_config.get("other_args", ""))
        command.extend(shlex.split(other_args))
        keys = process_config.get("keys")
        if keys:
            command.append("--keys")
            command.extend(shlex.split(keys) if isinstance(keys, str) else [str(key) for key in keys])
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

    def _process_replay_path(self, config: Mapping[str, Any]) -> Path:
        """Resolve and optionally render a process-specific replay file."""

        replay_value = config.get("replay")
        source = self.spec.replay_path if replay_value is None else (self.spec.path.parent / str(replay_value)).resolve()
        if not source.exists():
            raise ReplayConfigError(f"Replay file does not exist: {source}")
        if not (self.spec.urtest.get("template_replay", False) or config.get("template_replay", False)):
            return source
        if not source.is_file():
            raise ReplayConfigError(f"A replay directory cannot be used as a template: {source}")
        destination = self.sandbox / f"rendered-{source.name}"
        destination.write_text(self._replace_port_placeholders(source.read_text()))
        return destination

    def _resolve_test_path(self, value: Any, default: Path) -> Path:
        """Resolve an optional test-relative process input path."""

        if value is None:
            return default
        path = Path(str(value))
        return path if path.is_absolute() else (self.test_directory / path).resolve()

    def _run_pre_client_commands(self) -> None:
        """Run declared setup commands after ATS starts and before replay traffic."""

        self._run_ats_commands("pre_client_commands")

    def _run_post_client_commands(self) -> None:
        """Run declared mutations after replay traffic and before validation."""

        self._run_ats_commands("post_client_commands")

    def _run_ats_commands(self, key: str) -> None:
        """Run shell commands from one ATS replay lifecycle phase."""

        self._run_commands(
            self.spec.urtest["ats"].get(key, []),
            key,
            self.ats_paths,
            self.ats_environment,
        )

    def _run_commands(
        self,
        commands: Sequence[object],
        phase: str,
        paths: Mapping[str, Path],
        environment: Mapping[str, str] | None,
    ) -> None:
        """Run declared shell commands in the ATS tree during one lifecycle phase."""

        for command in commands:
            rendered = self._replace_runtime_placeholders(str(command), paths)
            result = subprocess.run(
                ["/bin/bash", "-o", "pipefail", "-c", rendered],
                cwd=paths["root"],
                env=environment,
                capture_output=True,
                text=True,
                timeout=30,
            )
            if result.returncode != 0:
                raise AssertionError(f"ATS {phase} command failed ({rendered}):\n{result.stdout}{result.stderr}")

    def _prepare_ats_tree(self, root: Path) -> dict[str, Path]:
        names = ("bin", "config", "body_factory", "plugin", "log", "runtime", "ssl", "storage", "cache")
        paths = {name: root / name for name in names}
        paths["root"] = root
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
        min_config = self.runtime.repository_root / "tests" / "tools" / "uranium" / "min_cfg"
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
        self._copy_ssl_files(config, paths)
        ssl_multicert_path = paths["config"] / "ssl_multicert.yaml"
        if isinstance(ssl_multicert, Mapping):
            write_yaml(ssl_multicert_path, ssl_multicert)
        else:
            ssl_multicert_path.write_text("\n".join(str(line) for line in ssl_multicert) + "\n")
        if "sni_yaml" in config:
            write_yaml(paths["config"] / "sni.yaml", self._replace_runtime_placeholders(config["sni_yaml"], paths))

    def _copy_ssl_files(self, config: Mapping[str, Any], paths: dict[str, Path]) -> None:
        """Copy test-owned TLS material for inbound or outbound TLS."""

        for value in config.get("copy_to_ssl_dir", []):
            source = (self.test_directory / str(value)).resolve()
            if not source.is_file():
                raise ReplayConfigError(f"TLS file does not exist: {source}")
            shutil.copy2(source, paths["ssl"] / source.name)

    def _write_ats_configs(self, config: Mapping[str, Any], paths: dict[str, Path]) -> None:
        config_dir = paths["config"]
        plugin_lines = [
            format_plugin_entry(self._replace_runtime_placeholders(entry, paths)) for entry in config.get("plugin_config", [])
        ]
        (config_dir / "plugin.config").write_text("\n".join(plugin_lines) + ("\n" if plugin_lines else ""))
        for plugin_value in config.get("copy_custom_plugin", []):
            source = self.runtime.resolve_artifact(self.test_directory, str(plugin_value))
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
            lines = [str(self._replace_runtime_placeholders(line, paths)) for line in config.get(key, [])]
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
            lines = [str(self._replace_runtime_placeholders(line, paths)) for line in config["remap_yaml_lines"]]
            (config_dir / "remap.yaml").write_text("\n".join(lines) + "\n")

        remap_lines = []
        for entry in config.get("remap_config", []):
            if isinstance(entry, str):
                remap_lines.append(str(self._replace_runtime_placeholders(entry, paths)))
                continue
            line = f"map {self._replace_runtime_placeholders(entry['from'], paths)} " \
                   f"{self._replace_runtime_placeholders(entry['to'], paths)}"
            for plugin in entry.get("plugins", []):
                line += f" @plugin={plugin['name']}"
                for argument in plugin.get("args", []):
                    line += f" @pparam={self._replace_runtime_placeholders(argument, paths)}"
            for option in entry.get("options", []):
                line += f" {self._replace_runtime_placeholders(option, paths)}"
            remap_lines.append(line)
        (config_dir / "remap.config").write_text("\n".join(remap_lines) + ("\n" if remap_lines else ""))

        for value in config.get("copy_to_config_dir", []):
            if isinstance(value, Mapping):
                source_value = str(value["source"])
                destination_value = str(value.get("destination", Path(source_value).name))
            else:
                source_value = destination_value = str(value)
            source = (self.test_directory / source_value).resolve()
            destination = config_dir / destination_value
            if source.is_dir():
                shutil.copytree(source, destination, dirs_exist_ok=True)
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        for value in config.get("template_to_config_dir", []):
            source = (self.test_directory / str(value)).resolve()
            if not source.is_file():
                raise ReplayConfigError(f"Configuration template does not exist: {source}")
            destination = config_dir / Path(str(value)).name
            rendered = self._replace_runtime_placeholders(source.read_text(), paths)
            destination.write_text(rendered)
        for filename in config.get("omit_config_files", []):
            (config_dir / str(filename)).unlink(missing_ok=True)
        for filename, content in config.get("inline_config_files", {}).items():
            destination = config_dir / str(filename)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(str(self._replace_runtime_placeholders(content, paths)))
        for filename, content in config.get("inline_runtime_files", {}).items():
            destination = paths["runtime"] / str(filename)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(str(self._replace_runtime_placeholders(content, paths)))
        for relative_path, content in config.get("body_factory_files", {}).items():
            destination = paths["body_factory"] / str(relative_path)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(str(self._replace_runtime_placeholders(content, paths)))

    def _replace_runtime_placeholders(self, value: Any, paths: Mapping[str, Path]) -> Any:
        """Replace dynamic listener and sandbox values in ATS metadata."""

        if isinstance(value, Mapping):
            return {key: self._replace_runtime_placeholders(item, paths) for key, item in value.items()}
        if isinstance(value, list):
            return [self._replace_runtime_placeholders(item, paths) for item in value]
        if not isinstance(value, str):
            return value
        replacements = {
            "{SERVER_HTTP_PORT}": str(self.server_http_port),
            "{SERVER_EXTRA_HTTP_PORT}": str(self.server_extra_http_port),
            "{SERVER_HTTPS_PORT}": str(self.server_https_port),
            "{ATS_HTTP_PORT}": str(self.http_port),
            "{ATS_HTTPS_PORT}": str(self.https_port),
            "{ATS_PROXY_PROTOCOL_PORT}": str(self.proxy_protocol_port),
            "{ATS_PROXY_PROTOCOL_HTTPS_PORT}": str(self.proxy_protocol_https_port),
            "{DNS_PORT}": str(self.dns_port or 0),
            "{UNUSED_PORT}": str(self.unused_port),
            "{ATS_ROOT}": str(paths["root"]),
            "{CONFIG_DIR}": str(paths["config"]),
            "{LOG_DIR}": str(paths["log"]),
            "{RUNTIME_DIR}": str(paths["rpc_runtime"]),
            "{STORAGE_DIR}": str(paths["storage"]),
            "{ATS_SSL_DIR}": str(paths["ssl"]),
            "{TEST_DIR}": str(self.test_directory),
            "{PYTHON}": sys.executable,
        }
        for marker, replacement in replacements.items():
            value = value.replace(marker, replacement)
        return self._replace_port_placeholders(value)

    def _replace_port_placeholders(self, value: str) -> str:
        """Replace replay placeholders that do not depend on an ATS tree."""

        replacements = {
            "{server_port}": self.server_extra_http_port,
            "{SERVER_HTTP_PORT}": self.server_http_port,
            "{SERVER_EXTRA_HTTP_PORT}": self.server_extra_http_port,
            "{SERVER_HTTPS_PORT}": self.server_https_port,
            "{ATS_HTTP_PORT}": self.http_port,
            "{ATS_HTTPS_PORT}": self.https_port,
            "{ATS_PROXY_PROTOCOL_PORT}": self.proxy_protocol_port,
            "{ATS_PROXY_PROTOCOL_HTTPS_PORT}": self.proxy_protocol_https_port,
            "{UNUSED_PORT}": self.unused_port,
        }
        for marker, replacement in replacements.items():
            value = value.replace(marker, str(replacement))
        return value

    def _check_metrics(self) -> None:
        """Poll until each expected ATS metric reaches its declared value."""

        checks = self.spec.urtest["ats"].get("metric_checks", [])
        for check in checks:
            metric = str(check["metric"])
            if "value" not in check and "min" not in check:
                raise ReplayConfigError(f"metric_checks entry for {metric} must specify value or min")
            time.sleep(float(check.get("delay", 2)))
            command = [self.ats_paths["bin"] / "traffic_ctl", "metric", "get", metric]
            deadline = time.monotonic() + float(check.get("timeout", 10))
            failure = f"Metric {metric} did not reach its expected value"
            while True:
                try:
                    result = subprocess.run(command, env=self.ats_environment, capture_output=True, text=True, timeout=10)
                except subprocess.TimeoutExpired:
                    failure = f"Timed out reading metric {metric}"
                else:
                    fields = result.stdout.split()
                    if result.returncode != 0:
                        failure = f"Could not read metric {metric}:\n{result.stdout}{result.stderr}"
                    elif len(fields) < 2:
                        failure = f"Metric {metric} was absent: {result.stdout!r}"
                    else:
                        actual_text = fields[-1]
                        matches_value = "value" not in check or re.fullmatch(str(check["value"]), actual_text) is not None
                        try:
                            matches_minimum = "min" not in check or float(actual_text) >= float(check["min"])
                        except ValueError:
                            matches_minimum = False
                        if matches_value and matches_minimum:
                            break
                        if not matches_value:
                            failure = f"Metric {metric} was {actual_text}, expected {check['value']}"
                        else:
                            failure = f"Metric {metric} was {actual_text}, expected at least {check['min']}"
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise AssertionError(failure)
                time.sleep(min(0.1, remaining))

    def _check_files(self) -> None:
        """Validate files produced or intentionally omitted by ATS."""

        checks = self.spec.urtest["ats"].get("file_checks", [])
        for check in checks:
            pattern = str(self._replace_runtime_placeholders(check.get("glob", check.get("path")), self.ats_paths))
            expected = bool(check.get("exists", True))
            timeout = float(check.get("timeout", 10 if expected else 0))
            deadline = time.monotonic() + timeout

            def matching_paths() -> list[Path]:
                return [Path(path) for path in glob.glob(pattern)
                       ] if "glob" in check else [Path(pattern)] if Path(pattern).exists() else []

            def is_ready() -> bool:
                paths = matching_paths()
                if bool(paths) != expected:
                    return False
                if not expected or "contains" not in check:
                    contents = [path.read_text(errors="replace") for path in paths]
                else:
                    contents = [path.read_text(errors="replace") for path in paths]
                    if not any(re.search(str(check["contains"]), content, re.MULTILINE) is not None for content in contents):
                        return False
                return "line_count_min" not in check or sum(len(content.splitlines()) for content in contents) >= int(
                    check["line_count_min"])

            while not is_ready() and time.monotonic() < deadline:
                time.sleep(0.1)
            paths = matching_paths()
            if bool(paths) != expected:
                state = "exist" if expected else "be absent"
                raise AssertionError(f"Expected {pattern} to {state}")
            if expected and "contains" in check:
                contents = [(path, path.read_text(errors="replace")) for path in paths]
                if not any(re.search(str(check["contains"]), content, re.MULTILINE) is not None for _, content in contents):
                    detail = "\n".join(f"--- {path} ---\n{content}" for path, content in contents)
                    raise AssertionError(f"Expected {pattern} to contain {check['contains']!r}\n{detail}")
            if expected and "matches" in check:
                content = "\n".join(path.read_text(errors="replace") for path in paths)
                for match in check["matches"]:
                    count = len(re.findall(str(match["expression"]), content, re.MULTILINE))
                    if "min" in match and count < int(match["min"]):
                        raise AssertionError(f"Expected at least {match['min']} matches for {match['expression']!r}, found {count}")
                    if "max" in match and count > int(match["max"]):
                        raise AssertionError(f"Expected at most {match['max']} matches for {match['expression']!r}, found {count}")

    def _validate_background_processes(self, server: ManagedProcess | None, ats: ManagedProcess) -> None:
        """Detect a server or unit-under-test exit before intentional teardown."""

        if ats.return_code is not None:
            raise AssertionError(f"ATS exited unexpectedly with status {ats.return_code}.\n{ats.output()}")
        if server is not None and server.return_code is not None and server.return_code not in server.return_codes:
            raise AssertionError(
                f"Proxy Verifier server exited with status {server.return_code}; expected "
                f"{sorted(server.return_codes)}.\n{server.output()}")

    def _validate_process_output(self, process: ManagedProcess, config: Mapping[str, Any], default_excludes: str) -> None:
        output = ""
        for path in (process.stdout_path, process.stderr_path):
            if path.exists():
                output += path.read_text(errors="replace")
        if default_excludes and re.search(default_excludes, output):
            raise AssertionError(f"Unexpected Proxy Verifier diagnostic in {process.name}:\n{output}")
        self._validate_text(output, config.get("log_validation", {}), process.name)

    def _validate_ats_logs(self) -> None:
        config = self.spec.urtest["ats"]
        diags_name = str(config.get("records_config", {}).get("proxy.config.diags.logfile.filename", "diags.log"))
        diags_path = self.ats_paths["log"] / diags_name
        if diags_name in ("stdout", "stderr"):
            diags_path = self.ats_paths["log"] / "traffic.out"
        if not config.get("process_config", {}).get("disable_log_checks", False):
            if not diags_path.is_file():
                raise AssertionError(f"ATS diagnostic log does not exist: {diags_path}")
            diags = diags_path.read_text(errors="replace")
            for expression in ("ERROR:", "FATAL:", "Unrecognized configuration value"):
                if expression in diags:
                    raise AssertionError(f"Unexpected {expression!r} in diags.log:\n{diags}")

        validation = config.get("log_validation", {})
        path_by_key = {
            "traffic_out": self.ats_paths["log"] / "traffic.out",
            "diags_log": diags_path,
            "error_log": self.ats_paths["log"] / "error.log",
        }
        for key, path in path_by_key.items():
            rules = validation.get(key, {})
            if rules:
                self._validate_text(path.read_text(errors="replace") if path.exists() else "", rules, key)
        access = validation.get("access_log", {})
        if access:
            access_path = self.ats_paths["log"] / access["filename"]
            if "gold_file" in access:
                assert_matches_gold(access_path, self.test_directory / access["gold_file"])
            else:
                self._validate_text(
                    access_path.read_text(errors="replace") if access_path.exists() else "", access, access_path.name)

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
            assert_matches_gold(actual_path, self.test_directory / rules["gold_file"])

    def _server_ports(
        self,
        enable_tls: bool,
        enable_quic: bool,
        enable_proxy_protocol: bool,
        enable_proxy_protocol_cp_src: bool,
    ) -> str:
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

    @staticmethod
    def _return_codes(config: Mapping[str, Any]) -> Iterable[int]:
        codes = config.get("return_code", 0)
        return codes if isinstance(codes, Sequence) and not isinstance(codes, str) else [codes]

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
