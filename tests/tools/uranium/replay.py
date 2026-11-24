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

from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any
import glob
import re
import subprocess
import time

from .assertions import assert_matches_gold
from .config import ReplayConfigError, ReplaySpec
from .process import return_codes
from .runtime import TestRuntime
from .services.ats import ATS
from .services.context import ProceduralContext
from .services.dns import DNSServer
from .services.process_service import ProcessService
from .services.verifier import VerifierClient, VerifierServer


class ReplaySkip(RuntimeError):
    """Signal that runtime capabilities do not satisfy replay requirements."""


class ReplayTest:
    """Run one replay file as one independently reportable test item."""

    def __init__(self, spec: ReplaySpec, runtime: TestRuntime, node_name: str | None = None) -> None:
        """Prepare service state without touching the scenario's sandbox.

        :param spec: Validated replay manifest and selected variant.
        :param runtime: Installed tools and shared session configuration.
        :param node_name: Pytest node identifier, defaulting to the file stem.
        """

        self.spec = spec
        self.runtime = runtime
        self.node_name = node_name or spec.path.stem
        self.sandbox = runtime.item_sandbox(self.node_name)
        # Every process the replay starts, in start order, so teardown can stop them in reverse.
        self.processes: list[ATS | ProcessService] = []
        self.ats = ATS.from_manifest(
            ProceduralContext(runtime, self.node_name, spec.path, self.sandbox),
            spec.urtest["ats"],
            test_directory=self.test_directory,
        )
        self.server_http_port = runtime.allocate_port()
        self.server_extra_http_port = runtime.allocate_port()
        self.server_https_port = runtime.allocate_port()
        self.unused_port = runtime.allocate_port()
        self.dns_port: int | None = None

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
        client = None
        ats_config = self.spec.urtest["ats"]
        try:
            try:
                self._start_dns()
                server = self._start_verifier_server()
                self._start_ats()
                self.ats.run_commands(ats_config.get("pre_client_commands", []), "pre_client_commands")
                client = self._run_verifier_client()
                self.ats.run_commands(ats_config.get("post_client_commands", []), "post_client_commands")
                self._validate_background_processes(server)
                if server is not None and self.spec.urtest["server"].get("stop_after_client", False):
                    server.stop()
                self._check_metrics()
                self._check_files()
            finally:
                self._stop_processes()

            if client is not None:
                self._validate_process_output(client, self.spec.urtest["client"])
            if server is not None:
                server.validate_output()
                self._validate_process_output(server, self.spec.urtest["server"])
            self.ats.validate_diagnostics()
            self._validate_ats_logs()
        finally:
            self.ats.remove_temporary_directories()

    def _stop_processes(self) -> None:
        """Stop every started process, newest first, even when stopping one of them fails."""

        failures = []
        for process in reversed(self.processes):
            try:
                process.stop()
            except Exception as error:
                failures.append(error)
        if failures:
            raise failures[0]

    def _start_dns(self) -> DNSServer | None:
        config = self.spec.urtest.get("dns")
        if config is None:
            return None
        name = str(config.get("name", "dns"))
        directory = self.sandbox / name
        directory.mkdir()
        dns = DNSServer.create(
            self.runtime,
            name,
            directory,
            default=config.get("process_config", {}).get("default", "127.0.0.1"),
            records=config.get("records", {}),
            test_directory=self.test_directory,
        )
        self.dns_port = dns.port
        self.ats.use_nameserver(dns.port)
        self.processes.append(dns)
        dns.start()
        return dns

    def _start_verifier_server(self) -> VerifierServer | None:
        config = self.spec.urtest["server"]
        if not config.get("enabled", True):
            return None
        process_config = config.get("process_config", {})
        name = str(config.get("name", "server"))
        directory = self.sandbox / name
        directory.mkdir()
        http_ports = [self.server_http_port]
        if process_config.get("listen_extra_http", False):
            http_ports.append(self.server_extra_http_port)
        server = VerifierServer.create(
            self.runtime,
            name,
            directory,
            self._process_replay_path(config),
            http_ports=http_ports,
            https_ports=[self.server_https_port],
            ssl_cert=self._resolve_test_path(process_config.get("ssl_cert")),
            ca_cert=self._resolve_test_path(process_config.get("ca_cert")),
            verbose=process_config.get("verbose", True),
            other_args=str(process_config.get("other_args", "")),
            return_codes=return_codes(config),
            test_directory=self.test_directory,
        )
        self.processes.append(server)
        server.start()
        return server

    def _start_ats(self) -> ATS:
        """Give ATS the placeholders and environment that name this replay's other processes, then start it."""

        configured_environment = self.spec.urtest["ats"].get("environment", {})
        for name, port in (("PV_HTTP_PORT", self.server_http_port), ("PV_HTTPS_PORT", self.server_https_port)):
            if name not in configured_environment:
                self.ats.set_environment(name, str(port))
        self.ats.define_placeholders(self._replay_placeholders())
        self.processes.append(self.ats)
        self.ats.start()
        return self.ats

    def _run_verifier_client(self) -> VerifierClient:
        config = self.spec.urtest["client"]
        process_config = config.get("process_config", {})
        name = str(config.get("name", "client"))
        directory = self.sandbox / name
        directory.mkdir()
        ats_process_config = self.spec.urtest["ats"].get("process_config", {})
        enable_tls = bool(ats_process_config.get("enable_tls", False))
        enable_quic = bool(ats_process_config.get("enable_quic", False))
        client = VerifierClient.create(
            self.runtime,
            name,
            directory,
            self._process_replay_path(config),
            http_ports=self._client_ports(process_config.get("http_ports", [self.ats.http_port])),
            https_ports=self._client_ports(process_config.get("https_ports", [self.ats.https_port] if enable_tls else [])),
            http3_ports=self._client_ports(process_config.get("http3_ports", [self.ats.https_port] if enable_quic else [])),
            ssl_cert=self._resolve_test_path(process_config.get("ssl_cert")),
            ca_cert=self._resolve_test_path(process_config.get("ca_cert")),
            keys=process_config.get("keys"),
            verbose=process_config.get("verbose", True),
            poll_timeout=process_config.get("poll_timeout"),
            other_args=str(process_config.get("other_args", "")),
            run_parallel=bool(process_config.get("run_parallel", False)),
            allow_errors=bool(process_config.get("allow_errors", False)),
            return_codes=return_codes(config),
            test_directory=self.test_directory,
        )
        self.processes.append(client)
        client.run(timeout=float(config.get("timeout", 60)))
        return client

    def _client_ports(self, ports: Sequence[object]) -> list[int]:
        """Resolve client target ports, which a manifest may name by placeholder.

        :param ports: Port numbers or placeholders such as ``{ATS_PROXY_PROTOCOL_PORT}``.
        """

        return [int(self._replace_port_placeholders(str(port))) for port in ports]

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

    def _resolve_test_path(self, value: Any) -> Path | None:
        """Resolve an optional test-relative process input path, or None when it is absent.

        :param value: Absolute path, path relative to the test directory, or None.
        """

        if value is None:
            return None
        path = Path(str(value))
        return path if path.is_absolute() else (self.test_directory / path).resolve()

    def _replay_placeholders(self) -> dict[str, object]:
        """Return the placeholders for the ports this replay owns, apart from ATS's own."""

        return {
            "{server_port}": self.server_extra_http_port,
            "{SERVER_HTTP_PORT}": self.server_http_port,
            "{SERVER_EXTRA_HTTP_PORT}": self.server_extra_http_port,
            "{SERVER_HTTPS_PORT}": self.server_https_port,
            "{DNS_PORT}": self.dns_port or 0,
            "{UNUSED_PORT}": self.unused_port,
        }

    def _replace_port_placeholders(self, value: str) -> str:
        """Replace port placeholders in values used before ATS has a run root, such as replay templates.

        :param value: Text that may contain ATS or replay port placeholders.
        """

        replacements = {
            "{ATS_HTTP_PORT}": self.ats.http_port,
            "{ATS_HTTPS_PORT}": self.ats.https_port,
            "{ATS_PROXY_PROTOCOL_PORT}": self.ats.proxy_protocol_port,
            "{ATS_PROXY_PROTOCOL_HTTPS_PORT}": self.ats.proxy_protocol_https_port,
            **self._replay_placeholders(),
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
            deadline = time.monotonic() + float(check.get("timeout", 10))
            failure = f"Metric {metric} did not reach its expected value"
            while True:
                try:
                    result = self.ats.traffic_ctl("metric", "get", metric, timeout=10)
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
            pattern = str(self.ats.replace_placeholders(check.get("glob", check.get("path"))))
            expected = bool(check.get("exists", True))
            timeout = float(check.get("timeout", 10 if expected else 0))
            deadline = time.monotonic() + timeout

            def matching_paths() -> list[Path]:
                return (
                    [Path(path) for path in glob.glob(pattern)]
                    if "glob" in check
                    else [Path(pattern)]
                    if Path(pattern).exists()
                    else []
                )

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
                    check["line_count_min"]
                )

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
            if expected and "excludes" in check:
                for path in paths:
                    content = path.read_text(errors="replace")
                    if re.search(str(check["excludes"]), content, re.MULTILINE):
                        raise AssertionError(f"Forbidden pattern {check['excludes']!r} in {path}:\n{content}")
            if expected and "line_count_min" in check:
                count = sum(len(path.read_text(errors="replace").splitlines()) for path in paths)
                if count < int(check["line_count_min"]):
                    raise AssertionError(f"Expected at least {check['line_count_min']} lines in {pattern}, found {count}")

    def _validate_background_processes(self, server: VerifierServer | None) -> None:
        """Detect a server or unit-under-test exit before intentional teardown.

        :param server: The replay's verifier-server, or None when it is disabled.
        """

        if self.ats.return_code is not None:
            raise AssertionError(f"ATS exited unexpectedly with status {self.ats.return_code}.\n{self.ats.process_output}")
        if server is not None and server.return_code is not None and server.return_code not in server.return_codes:
            raise AssertionError(
                f"Proxy Verifier server exited with status {server.return_code}; expected "
                f"{sorted(server.return_codes)}.\n{server.output}"
            )

    def _validate_process_output(self, service: ProcessService, config: Mapping[str, Any]) -> None:
        """Apply a manifest's ``log_validation`` rules to a Proxy Verifier process's output.

        :param service: Stopped Proxy Verifier process.
        :param config: The process's ``urtest.client`` or ``urtest.server`` mapping.
        """

        self._validate_text(service.output, config.get("log_validation", {}), service.name)

    def _validate_ats_logs(self) -> None:
        """Apply the manifest's ``log_validation`` rules to ATS's logs."""

        validation = self.spec.urtest["ats"].get("log_validation", {})
        path_by_key = {
            "traffic_out": self.ats.traffic_out,
            "diags_log": self.ats.diags_log,
            "error_log": self.ats.error_log,
        }
        for key, path in path_by_key.items():
            rules = validation.get(key, {})
            if rules:
                self._validate_text(path.read_text(errors="replace") if path.exists() else "", rules, key)
        access = validation.get("access_log", {})
        if access:
            access_path = self.ats.log_directory / access["filename"]
            if "gold_file" in access:
                assert_matches_gold(access_path, self.test_directory / access["gold_file"])
            else:
                self._validate_text(
                    access_path.read_text(errors="replace") if access_path.exists() else "", access, access_path.name
                )

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
