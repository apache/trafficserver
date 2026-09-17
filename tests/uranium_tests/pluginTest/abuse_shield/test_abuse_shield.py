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
"""Verify abuse_shield configuration, enforcement, and operations."""

from pathlib import Path
import hashlib
import re
import sys
import time

import pytest
import yaml

from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory, wait_for_file_lines

TEST_DIRECTORY = Path(__file__).parent


class AbuseShieldHarness:
    """Build isolated abuse_shield scenarios with common clients and assertions."""

    def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
        """Remember the factories used by one pytest scenario.

        :param ats_factory: Factory for isolated Traffic Server instances.
        :param services: Factory for origins and custom clients.
        :param curl: Curl command runner.
        """

        self._ats_factory = ats_factory
        self._services = services
        self._curl = curl

    def origin(self) -> OriginServer:
        """Create the standard cacheable origin used by flood tests."""

        origin = self._services.origin("origin")
        origin.add_response(
            {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "body": ""
            },
            {
                "headers":
                    (
                        "HTTP/1.1 200 OK\r\nServer: origin\r\nCache-Control: max-age=300\r\n"
                        "Connection: close\r\nContent-Length: 2\r\n\r\n"),
                "body": "OK",
            },
        )
        return origin

    def ats(
        self,
        config: str,
        *,
        origin: OriginServer | None = None,
        tls: bool = True,
        jax: bool = False,
    ) -> ATS:
        """Configure ATS with one abuse_shield document.

        :param config: Complete abuse_shield YAML content.
        :param origin: Optional microserver receiving mapped requests.
        :param tls: Whether to enable the TLS listener.
        :param jax: Whether to load and export JA3 fingerprints.
        """

        ats = self._ats_factory.create("ts", enable_tls=tls, enable_cache=True)
        if not ats.plugin_exists("abuse_shield.so"):
            pytest.skip("abuse_shield.so is required")
        if jax and not ats.plugin_exists("jax_fingerprint.so"):
            pytest.skip("jax_fingerprint.so is required")
        if tls:
            ats.add_default_ssl_files()
        ats.records.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "abuse_shield|jax_fingerprint" if jax else "abuse_shield",
            })
        if origin is not None:
            ats.remap_config.add_line(f"map / http://127.0.0.1:{origin.port}/")
        ats.write_config_file("abuse_shield.yaml", config)
        if jax:
            ats.plugin_config.add_line("jax_fingerprint.so --method JA3 --export abuse_shield.fingerprints")
        ats.plugin_config.add_line("abuse_shield.so abuse_shield.yaml")
        return ats

    def helper(self, name: str, script: str, *arguments: str, timeout: float = 30) -> str:
        """Run one Python helper and return its combined output.

        :param name: Unique process name within this test.
        :param script: Helper filename in the test directory.
        :param arguments: Command-line arguments passed to the helper.
        :param timeout: Maximum number of seconds to wait.
        """

        client = self._services.process(name, (sys.executable, TEST_DIRECTORY / script, *arguments))
        result = client.run(timeout=timeout)
        assert result.returncode == 0, result.output
        return result.output

    @staticmethod
    def start(ats: ATS) -> None:
        """Require every configured rule to load before driving traffic.

        :param ats: Configured abuse_shield instance to start.
        """

        ats.start()
        config = yaml.safe_load((ats.config_directory / "abuse_shield.yaml").read_text())
        count = len(config.get("rules", []))
        wait_for_file_lines(ats.diags_log, rf"Plugin initialized with 1000 slots per tracker, {count} rules\b", 1)

    @staticmethod
    def metric(ats: ATS, name: str, expression: str, timeout: float = 10) -> str:
        """Wait until one metric value satisfies a regular expression.

        :param ats: Running Traffic Server instance.
        :param name: Metric name queried with ``traffic_ctl``.
        :param expression: Regular expression matched against output.
        :param timeout: Maximum number of seconds to wait.
        """

        deadline = time.monotonic() + timeout
        output = ""
        while time.monotonic() < deadline:
            result = ats.traffic_ctl("metric", "get", name)
            output = result.output
            if result.returncode == 0 and re.search(expression, output):
                return output
            time.sleep(0.1)
        raise AssertionError(f"Metric {name} did not match {expression!r}:\n{output}")

    def h2_flood(
        self,
        ats: ATS,
        name: str,
        *,
        count: int,
        rate: int,
        reset_streams: bool = False,
        host: str = "localhost",
    ) -> None:
        """Run the HTTP/2 request-rate helper.

        :param ats: Target Traffic Server instance.
        :param name: Unique helper process name.
        :param count: Number of requests to send.
        :param rate: Target requests per second.
        :param reset_streams: Whether each stream is reset with CANCEL.
        :param host: TLS authority and target hostname.
        """

        arguments = [
            "--host",
            host,
            "--port",
            str(ats.https_port),
            "--num-requests",
            str(count),
            "--rate",
            str(rate),
            "--path",
            "/",
        ]
        if reset_streams:
            arguments.append("--reset-streams")
        self.helper(name, "h2_rate_client.py", *arguments)


def test_abuse_shield_messages(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Plugin messages mutate state and trusted clients bypass enforcement.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for custom clients.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    trusted = "trusted_ips:\n  - 127.0.0.1\n  - '::1'\n  - '::ffff:127.0.0.1'\n"
    config = (
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "  trusted_ips_file: {trusted}\n"
        "rules:\n"
        "  - {name: test_h2_error_rule, filter: {max_h2_error_rate: 5}, action: [log, block]}\n"
        "  - {name: test_request_rule, filter: {max_req_rate: 10}, action: [log, block, close]}\n"
        "enabled: true\n")
    ats = harness.ats(config)
    ats.write_config_file("abuse_shield_trusted.yaml", trusted)
    config_path = ats.config_directory / "abuse_shield.yaml"
    ats.write_config_file("abuse_shield.yaml", config.replace("{trusted}", str(ats.config_directory / "abuse_shield_trusted.yaml")))
    harness.start(ats)
    wait_for_file_lines(ats.diags_log, r"Plugin initialized with 1000 slots per tracker, 2 rules", 1)

    for topic, value, marker in (
        ("abuse_shield.enabled", "false", "Plugin disabled"),
        ("abuse_shield.enabled", "true", "Plugin enabled"),
        ("abuse_shield.dump", "", "abuse_shield dump"),
        ("abuse_shield.stats", "", "Stats synced"),
        ("abuse_shield.reset", "", "Metrics reset"),
        ("abuse_shield.trusted", "", "Trusted IP ranges (3 total)"),
        ("abuse_shield.reload", "", "Configuration reloaded successfully"),
    ):
        arguments = ("plugin", "msg", topic, value) if value else ("plugin", "msg", topic)
        result = ats.traffic_ctl(*arguments)
        assert result.returncode == 0, result.output
        wait_for_file_lines(ats.diags_log, re.escape(marker), 1)

    flood = ats.run_shell(f"seq 1 30 | xargs -P 30 -I {{}} curl -s -o /dev/null http://127.0.0.1:{ats.http_port}/")
    assert flood.returncode == 0, flood.output
    harness.metric(ats, "abuse_shield.actions.blocked", r"abuse_shield.actions.blocked\s+0\b")
    assert config_path.exists()
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert 'Rule "test_request_rule" matched' not in diagnostics


def test_abuse_shield_request_rates(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """HTTP/2 request and reset floods trigger their independent rules.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for origin and helper clients.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n  log_interval_sec: 0\n"
        "rules:\n"
        "  - {name: req_rate_flood, filter: {max_req_rate: 20}, action: [log, block]}\n"
        "  - {name: h2_error_flood, filter: {max_h2_error_rate: 2}, action: [log]}\n"
        "enabled: true\n",
        origin=origin,
    )
    origin.start()
    harness.start(ats)
    harness.h2_flood(ats, "reset-client", count=10, rate=100, reset_streams=True)
    wait_for_file_lines(ats.diags_log, r'Rule "h2_error_flood" matched for IP=.*actions=\[log\]', 1)
    harness.h2_flood(ats, "request-client", count=50, rate=100)
    wait_for_file_lines(ats.diags_log, r'Rule "req_rate_flood" matched for IP=.*actions=\[log,block\]', 1)


def test_abuse_shield_connection_rate(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Idle TLS connections trigger blocking before ClientHello.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the idle-connection helper.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "rules:\n  - {name: conn_rate_flood, filter: {max_conn_rate: 5}, action: [log, block]}\n"
        "enabled: true\n")
    harness.start(ats)
    harness.helper("idle-client", "idle_connections.py", "--port", str(ats.https_port), "--count", "30")
    wait_for_file_lines(ats.diags_log, r'Rule "conn_rate_flood" matched for IP=.*actions=\[log,block\]', 1)
    harness.metric(ats, "abuse_shield.actions.blocked", r"abuse_shield.actions.blocked\s+[1-9][0-9]*")
    harness.metric(ats, "abuse_shield.connections.rejected", r"abuse_shield.connections.rejected\s+[1-9][0-9]*")


def test_abuse_shield_rate_limited_ips(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Rate-limited IP rules replace ordinary rules for request rates.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for origin and helper clients.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats("enabled: true\n", origin=origin)
    rate_limited = ats.config_directory / "rate_limited.yaml"
    ats.write_config_file("rate_limited.yaml", "rate_limited_ips:\n  - 127.0.0.1\n  - '::1'\n  - '::ffff:127.0.0.1'\n")
    ats.write_config_file(
        "abuse_shield.yaml",
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "rules:\n"
        "  - {name: ordinary_req, filter: {max_req_rate: 5}, action: [log, block]}\n"
        f"  - name: rate_limited_req\n    filter:\n      max_req_rate: 100\n      rate_limited_ips_file: {rate_limited}\n"
        "    action: [log]\nenabled: true\n",
    )
    origin.start()
    harness.start(ats)
    harness.h2_flood(ats, "below-rate-limited", count=30, rate=50)
    harness.h2_flood(ats, "above-rate-limited", count=250, rate=1000)
    diagnostics = wait_for_file_lines(ats.diags_log, r'Rule "rate_limited_req" matched for IP=.*actions=\[log\]', 1)
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert 'Rule "ordinary_req" matched' not in diagnostics


def test_abuse_shield_http_block(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Plain HTTP sessions are rejected by a connection-rate block.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the origin.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "rules:\n  - {name: http_conn_rate_flood, filter: {max_conn_rate: 10}, action: [log, block]}\n"
        "enabled: true\n",
        origin=origin,
        tls=False,
    )
    origin.start()
    harness.start(ats)
    flood = ats.run_shell(f"seq 1 50 | xargs -P 50 -I {{}} curl --max-time 5 -s http://127.0.0.1:{ats.http_port}/")
    assert flood.returncode == 123, flood.output
    wait_for_file_lines(ats.diags_log, r'Rule "http_conn_rate_flood" matched for IP=.*actions=\[log,block\]', 1)
    harness.metric(ats, "abuse_shield.actions.blocked", r"abuse_shield.actions.blocked\s+[1-9][0-9]*")
    harness.metric(ats, "abuse_shield.connections.rejected", r"abuse_shield.connections.rejected\s+[1-9][0-9]*")


def test_abuse_shield_multiple_rules(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A lenient first rule does not inherit a stricter rule's debt.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for origin and helper clients.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "rules:\n"
        "  - {name: lenient_limit, filter: {max_req_rate: 100}, action: [log]}\n"
        "  - {name: strict_limit, filter: {max_req_rate: 15}, action: [log, block]}\n"
        "enabled: true\n",
        origin=origin,
    )
    origin.start()
    harness.start(ats)
    harness.h2_flood(ats, "multi-client", count=50, rate=100)
    diagnostics = wait_for_file_lines(ats.diags_log, r'Rule "strict_limit" matched for IP=.*actions=\[log,block\]', 1)
    ats.stop()
    diagnostics = ats.diags_log.read_text(errors="replace")
    assert 'Rule "lenient_limit" matched' not in diagnostics


def test_abuse_shield_block_expiration(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """An expired temporary block permits the client again.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for origin and helper clients.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 5}\n"
        "rules:\n  - {name: short_block, filter: {max_req_rate: 10}, action: [log, block]}\n"
        "enabled: true\n",
        origin=origin,
    )
    origin.start()
    harness.start(ats)
    harness.h2_flood(ats, "expiration-client", count=50, rate=100, host="127.0.0.1")
    wait_for_file_lines(ats.diags_log, r'Rule "short_block" matched for IP=.*actions=\[log,block\]', 1)
    blocked = curl.run(f"--insecure --silent --output /dev/null --max-time 2 https://127.0.0.1:{ats.https_port}/")
    assert blocked.returncode in (28, 35, 52, 55, 56), blocked.output
    harness.metric(ats, "abuse_shield.actions.blocked", r"abuse_shield.actions.blocked\s+[1-9][0-9]*")
    harness.metric(ats, "abuse_shield.connections.rejected", r"abuse_shield.connections.rejected\s+[1-9][0-9]*")
    time.sleep(7)
    recovered = curl.run(f'--insecure --silent --output /dev/null --write-out "%{{http_code}}" https://127.0.0.1:{ats.https_port}/')
    assert recovered.returncode == 0, recovered.output
    assert recovered.stdout == "200", recovered.output


def test_abuse_shield_combined_rule(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """One rule triggers only after both connection and request rates rise.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for the origin.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "rules:\n  - {name: combined_abuse, filter: {max_conn_rate: 5, max_req_rate: 10}, action: [log, block]}\n"
        "enabled: true\n",
        origin=origin,
    )
    origin.start()
    harness.start(ats)
    flood = ats.run_shell(f"seq 1 30 | xargs -P 30 -I {{}} curl --max-time 5 -k -s https://127.0.0.1:{ats.https_port}/")
    assert flood.returncode == 123, flood.output
    wait_for_file_lines(ats.diags_log, r'Rule "combined_abuse" matched for IP=.*actions=\[log,block\]', 1)
    harness.metric(ats, "abuse_shield.actions.blocked", r"abuse_shield.actions.blocked\s+[1-9][0-9]*")
    harness.metric(ats, "abuse_shield.connections.rejected", r"abuse_shield.connections.rejected\s+[1-9][0-9]*")


def test_abuse_shield_log_file(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """The log action writes its configured dedicated file.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for origin and helper clients.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    origin = harness.origin()
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  blocking: {duration_seconds: 60}\n"
        "  log_file: abuse_shield_actions\n"
        "rules:\n  - {name: log_test_rule, filter: {max_req_rate: 10}, action: [log, block]}\n"
        "enabled: true\n",
        origin=origin,
    )
    origin.start()
    harness.start(ats)
    harness.h2_flood(ats, "log-client", count=50, rate=100)
    content = wait_for_file_lines(ats.log_directory / "abuse_shield_actions.log", r'Rule "log_test_rule" matched for IP=', 1)
    assert "req_tokens=" in content


def test_abuse_shield_fingerprint(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """A reloaded JA3 denylist rejects ClientHello before ServerHello.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory for deterministic ClientHello helpers.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    source = "771,49199,0-10-11-13-43,29,0"
    client_ja3 = hashlib.md5(source.encode("ascii")).hexdigest()
    nonmatching = "00000000000000000000000000000000"
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  log_interval_sec: 0\n"
        "  fingerprint_registry: abuse_shield.fingerprints\n"
        "rules:\n  - name: blocked_ja3\n    filter:\n      fingerprints:\n        JA3:\n"
        f'          - "{nonmatching}"\n    action: [log, close]\nenabled: true\n',
        jax=True,
    )
    harness.start(ats)
    wait_for_file_lines(ats.diags_log, "Using JAx fingerprint registry 'abuse_shield.fingerprints'", 1)

    client_args = ("--host", "127.0.0.1", "--port", str(ats.https_port), "--expect")
    harness.helper("allowed-client", "tls_client_hello.py", *client_args, "response")
    config_path = ats.config_directory / "abuse_shield.yaml"
    original = config_path.read_text()
    config_path.write_text(original.replace("action: [log, close]", "action: close"))
    reload_result = ats.traffic_ctl("plugin", "msg", "abuse_shield.reload")
    assert reload_result.returncode == 0, reload_result.output
    wait_for_file_lines(ats.diags_log, "Configuration reload failed", 1)
    harness.helper("still-allowed-client", "tls_client_hello.py", *client_args, "response")

    config_path.write_text(original.replace(nonmatching, client_ja3))
    reload_result = ats.traffic_ctl("plugin", "msg", "abuse_shield.reload")
    assert reload_result.returncode == 0, reload_result.output
    wait_for_file_lines(ats.diags_log, "Configuration reloaded successfully", 1)
    harness.helper("rejected-client", "tls_client_hello.py", *client_args, "reject")
    wait_for_file_lines(
        ats.diags_log,
        rf'Rule "blocked_ja3" matched for IP=.*fingerprint=JA3:{client_ja3}.*actions=\[log,close\]',
        1,
    )
    harness.metric(ats, "abuse_shield.fingerprints.rejected", r"abuse_shield.fingerprints.rejected\s+1\b")


def test_abuse_shield_shared_log_interval(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
    """Connection and transaction hooks share one per-IP log interval.

    :param ats_factory: Factory for isolated Traffic Server instances.
    :param services: Factory retained by the shared harness.
    :param curl: Curl command runner.
    """

    harness = AbuseShieldHarness(ats_factory, services, curl)
    ats = harness.ats(
        "global:\n  ip_tracking: {slots: 1000}\n  log_interval_sec: 3600\n"
        "rules:\n"
        "  - {name: connection_log, filter: {max_conn_rate: 1}, action: [log]}\n"
        "  - {name: request_log, filter: {max_req_rate: 1}, action: [log]}\n"
        "enabled: true\n",
        tls=False,
    )
    harness.start(ats)
    flood = ats.run_shell(f"seq 1 100 | xargs -P 16 -I {{}} curl --max-time 5 -s -o /dev/null http://127.0.0.1:{ats.http_port}/")
    assert flood.returncode == 0, flood.output
    wait_for_file_lines(ats.diags_log, r'Rule "(connection|request)_log" matched for IP=127\.0\.0\.1 actions=\[log\]', 1)
    harness.metric(ats, "abuse_shield.actions.logged", r"abuse_shield.actions.logged\s+1\b")
    harness.metric(ats, "abuse_shield.rules.matched", r"abuse_shield.rules.matched\s+[1-9][0-9]+\b")
