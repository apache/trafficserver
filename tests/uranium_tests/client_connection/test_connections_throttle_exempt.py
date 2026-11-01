"""Verify that proxy.config.http.per_client.connection.exempt_list exempts client connections from
proxy.config.net.connections_throttle."""
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

from collections.abc import Iterable, Mapping
import re
import time
from typing import Any

import pytest

from tools.uranium.services import ATS, ATSFactory, ServiceFactory, wait_for_file_lines

from .throttle_client import attempt, hold, request

EXEMPT_LIST_VAR = "proxy.config.http.per_client.connection.exempt_list"
OPEN = "proxy.process.net.connections_currently_open"
EXEMPT_OPEN = "proxy.process.net.per_client.connections_exempt_currently_open"
THROTTLED = "proxy.process.net.connections_throttled_in"
MEMORY_RSS = "proxy.process.traffic_server.memory.rss"
EXEMPT = "proxy.process.net.per_client.connections_exempt_in"

# An IPv4 and an IPv6 prefix longer than the address. If either were read as /0, it would exempt the first address, the
# loopback address of its family. The second address is the loopback address of the other family.
INVALID_PREFIXES = [
    pytest.param("192.0.2.1/256", "127.0.0.1", "::1", id="ipv4"),
    pytest.param("2001:db8::1/256", "::1", "127.0.0.1", id="ipv6"),
]


def invalid_entry_warning(entry: str) -> str:
    """Return a regular expression for the warning that rejects the exempt list entry @a entry."""

    return re.escape(f"WARNING: {EXEMPT_LIST_VAR}: '{entry}' is not a valid IP range")


def create_ats(ats_factory: ATSFactory, name: str, **options: Any) -> ATS:
    """Create a Traffic Server that serves the health check with the statichit plugin, so that it needs no origin.

    :param name: The process name.
    :param options: The options of ATSFactory.create().
    """

    ats = ats_factory.create(name, **options)
    if not ats.plugin_exists("statichit.so"):
        pytest.skip("statichit.so is required")
    ats.write_config_file("healthcheck.txt", "OK\n")
    healthcheck = ats.config_directory / "healthcheck.txt"
    ats.remap_config.add_line(f"map / http://127.0.0.1/ @plugin=statichit.so @pparam=--file-path={healthcheck}")
    return ats


def metrics(ats: ATS, names: Iterable[str]) -> dict[str, int]:
    """Return the values of the metrics @a names, read in one traffic_ctl call."""

    result = ats.traffic_ctl("metric", "get", *names)
    assert result.returncode == 0, result.output
    return {name: int(value) for name, value in (line.split() for line in result.stdout.splitlines())}


def wait_for_metrics(ats: ATS, expected: Mapping[str, int], timeout: float = 10) -> None:
    """Wait until one read of the metrics shows each metric in @a expected at its value.

    :param timeout: How many seconds to wait.
    """

    deadline = time.monotonic() + timeout
    values = metrics(ats, expected)
    while values != expected and time.monotonic() < deadline:
        time.sleep(0.1)
        values = metrics(ats, expected)
    assert values == expected, f"Expected the metrics {expected}, found {values}"


def stop_and_check_diags(ats: ATS) -> None:
    """Stop @a ats and require that diags.log has no errors. Teardown checks only for FATAL."""

    ats.stop()
    diags = ats.diags_log.read_text(errors="replace")
    assert "ERROR:" not in diags, f"diags.log should contain no errors:\n{diags}"


class ThrottledTrafficServer:
    """A Traffic Server whose connections_throttle is filled by connections from one address."""

    # An accept is refused once int(connections_currently_open * 1.1) >= connections_throttle,
    # so for a throttle below 11 the held connections fill it exactly.
    THROTTLE = 5

    def __init__(
            self,
            ats_factory: ATSFactory,
            services: ServiceFactory,
            exempt_list: str,
            held: str,
            other: str,
            accept_threads: int = 1) -> None:
        """Start Traffic Server and fill connections_throttle.

        :param exempt_list: The value of proxy.config.http.per_client.connection.exempt_list.
        :param held: The address, 127.0.0.1 or ::1, of the connections that fill the throttle.
        :param other: The other address.
        :param accept_threads: The value of proxy.config.accept_threads, which selects the accept path.
        """

        self.held = held
        self.other = other
        # The expected counter values. None when a retry makes the count unknown.
        self._throttled = 0
        self._exempt_count: int | None = 0
        self.ats = self._configure(ats_factory, services, exempt_list, accept_threads)
        self.ats.start()
        self._held = hold(held, self._port(held, "plain"), self.THROTTLE)
        self._wait_for_counters()

    def _configure(self, ats_factory: ATSFactory, services: ServiceFactory, exempt_list: str, accept_threads: int) -> ATS:
        """Configure Traffic Server with plain, TLS, allow-plain, and PROXY protocol ports in both families."""

        ats = create_ats(ats_factory, "ts", enable_tls=True)
        self._ports = {
            "plain": (ats.http_port, ats.ipv6_port),
            "tls": (ats.https_port, ats.ipv6_https_port),
            "allow-plain": (services.allocate_port(), services.allocate_port()),
            "proxy": (ats.proxy_protocol_port, services.allocate_port()),
        }
        flags = {"plain": "", "tls": ":ssl", "allow-plain": ":ssl:allow-plain", "proxy": ":pp"}
        server_ports = " ".join(f"{ipv4}{flags[kind]} {ipv6}{flags[kind]}:ipv6" for kind, (ipv4, ipv6) in self._ports.items())
        ats.records.update(
            {
                "proxy.config.http.server_ports": server_ports,
                "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1,::1",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "iocore_net_accept|statichit",
                "proxy.config.accept_threads": accept_threads,
                "proxy.config.net.connections_throttle": self.THROTTLE,
                EXEMPT_LIST_VAR: exempt_list,
                # Hold the incomplete requests for the whole test.
                "proxy.config.http.accept_no_activity_timeout": 300,
                "proxy.config.http.transaction_no_activity_timeout_in": 300,
            })
        return ats

    def _port(self, host: str, kind: str) -> int:
        """Return the port of @a kind ('plain', 'tls', 'allow-plain' or 'proxy') in the family of @a host."""

        return self._ports[kind][1 if ":" in host else 0]

    def _wait_for_counters(self) -> None:
        """Wait for the counters to reach their expected values, with no exempt connection open."""

        expected = {OPEN: self.THROTTLE, EXEMPT_OPEN: 0, THROTTLED: self._throttled}
        if self._exempt_count is not None:
            expected[EXEMPT] = self._exempt_count
        wait_for_metrics(self.ats, expected)

    def refused(self, host: str, kind: str = "plain", proxy_source: str = "", retry: float = 0) -> None:
        """Check that a connection from @a host is refused and counted in connections_throttled_in.

        :param proxy_source: Claim this source address in a PROXY protocol header.
        :param retry: Retry for this many seconds, for a reload to take effect.
        """

        self._throttled += 1
        if retry:
            # The attempts that are served before the reload takes effect are counted as exempt.
            self._exempt_count = None
        request(host, self._port(host, kind), "refused", retry=retry, tls=kind == "tls", proxy_source=proxy_source)
        self._wait_for_counters()

    def served(self, kind: str = "plain", proxy_source: str = "") -> None:
        """Check that a connection from the other address is served, and counted as an exempt open connection.

        :param proxy_source: Claim this source address in a PROXY protocol header.
        """

        assert self._exempt_count is not None, "A served check needs a known exempt count."
        self._exempt_count += 1
        open_counts: dict[str, int] = {}

        def read_open_counts() -> None:
            open_counts.update(metrics(self.ats, (OPEN, EXEMPT_OPEN)))

        outcome = attempt(
            self.other, self._port(self.other, kind), tls=kind == "tls", proxy_source=proxy_source, while_open=read_open_counts)
        assert outcome == "served", f"A {kind} connection from {self.other} was refused"
        assert open_counts == {OPEN: self.THROTTLE + 1, EXEMPT_OPEN: 1}, "The open exempt connection is counted as exempt."
        self._wait_for_counters()

    def reload(self, exempt_list: str, warning: str = "") -> None:
        """Set the exempt list with traffic_ctl and reload.

        :param warning: Wait for this regular expression in diags.log after the reload.
        """

        for command in (("config", "set", EXEMPT_LIST_VAR, exempt_list), ("config", "reload")):
            result = self.ats.traffic_ctl(*command)
            assert result.returncode == 0, result.output
        if warning:
            wait_for_file_lines(self.ats.diags_log, warning, 1)

    def finish(self) -> None:
        """Close the held connections, stop Traffic Server, and check diags.log."""

        for sock in self._held:
            sock.close()
        stop_and_check_diags(self.ats)


def test_memory_limit_applies(ats_factory: ATSFactory) -> None:
    """Check that the memory limit still refuses a client in the list."""

    ats = create_ats(ats_factory, "ts")
    ats.records.update(
        {
            "proxy.config.http.server_ports": f"{ats.http_port} {ats.ipv6_port}:ipv6",
            EXEMPT_LIST_VAR: "::1",
            # Less than any resident set size, so every new client connection is refused once memory use is checked.
            "proxy.config.memory.max_usage": 1,
        })
    ats.start()
    # Traffic Server checks memory use every 10 seconds, and serves the health check until then.
    request("::1", ats.ipv6_port, "refused", retry=30)
    wait_for_metrics(ats, {THROTTLED: 1})
    # The throttle is on, so the published RSS is above the 1 byte limit, and it is a real byte count.
    rss = metrics(ats, [MEMORY_RSS])[MEMORY_RSS]
    assert 1 < rss < 1 << 40, f"Expected a plausible {MEMORY_RSS}, found {rss}"
    stop_and_check_diags(ats)


# accept_threads 1 accepts in a dedicated thread; 0 accepts on each ET_NET thread.
@pytest.mark.parametrize("accept_threads", [1, 0], ids=["accept_threads_1", "accept_threads_0"])
def test_exempt_ipv6(ats_factory: ATSFactory, services: ServiceFactory, accept_threads: int) -> None:
    """Fill the throttle from 127.0.0.1, and check ::1 over each kind of port, and a reload of the list."""

    # A network, a range, and an empty entry that match no test client, and ::1.
    t = ThrottledTrafficServer(
        ats_factory,
        services,
        "192.0.2.0/24, 198.51.100.1-198.51.100.9, ::1,",
        held="127.0.0.1",
        other="::1",
        accept_threads=accept_threads)
    # IPv4 is refused at the limit.
    t.refused(t.held)
    # ::1 is served over TCP, TLS, plain HTTP on an allow-plain TLS port, and a PROXY protocol port that claims an
    # address not in the list.
    t.served()
    t.served(kind="tls")
    t.served(kind="allow-plain")
    t.served(kind="proxy", proxy_source="2001:db8::1")
    # IPv4 is refused on a PROXY protocol port that claims ::1.
    t.refused(t.held, kind="proxy", proxy_source="::1")
    t.refused(t.held)

    # The previous list stays in force after an invalid reload.
    t.reload("192.0.2.1, not-an-address", invalid_entry_warning("not-an-address"))
    t.served()

    # ::1 is refused once the list does not name it.
    t.reload("192.0.2.1")
    t.refused(t.other, retry=10)
    t.finish()


@pytest.mark.parametrize("exempt_list", ["127.0.0.0/8", "127.0.0.1-127.0.0.3"], ids=["cidr", "range"])
def test_exempt_ipv4(ats_factory: ATSFactory, services: ServiceFactory, exempt_list: str) -> None:
    """Fill the throttle from ::1, and check that a CIDR network or a range matches 127.0.0.1."""

    t = ThrottledTrafficServer(ats_factory, services, exempt_list, held="::1", other="127.0.0.1")
    t.refused(t.held)
    t.served()
    t.refused(t.held)
    t.finish()


@pytest.mark.parametrize(("prefix", "covered", "other"), INVALID_PREFIXES)
def test_invalid_prefix_at_startup(
        ats_factory: ATSFactory, services: ServiceFactory, prefix: str, covered: str, other: str) -> None:
    """Check that a list with an invalid prefix is rejected at startup, and exempts no address.

    :param prefix: An exempt list entry with a prefix longer than the address.
    :param covered: The address that @a prefix would exempt if it were read as /0.
    :param other: An address in the other family, which fills the throttle.
    """

    t = ThrottledTrafficServer(ats_factory, services, prefix, held=other, other=covered)
    wait_for_file_lines(t.ats.diags_log, invalid_entry_warning(prefix), 1)
    t.refused(covered)
    t.finish()


@pytest.mark.parametrize(("prefix", "covered", "other"), INVALID_PREFIXES)
def test_invalid_prefix_on_reload(ats_factory: ATSFactory, services: ServiceFactory, prefix: str, covered: str, other: str) -> None:
    """Check that a reload to a list with an invalid prefix keeps the previous list.

    :param prefix: An exempt list entry with a prefix longer than the address.
    :param covered: The address that @a prefix would exempt if it were read as /0, which fills the throttle.
    :param other: An address in the other family, which the previous list names.
    """

    t = ThrottledTrafficServer(ats_factory, services, other, held=covered, other=other)
    t.served()
    t.reload(prefix, invalid_entry_warning(prefix))
    t.served()
    t.refused(covered)
    t.finish()
