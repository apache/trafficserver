'''
Verify the behavior of proxy.config.http.per_server.connection.max and the per server
connection metrics (proxy.config.http.per_server.connection.metric_enabled and
proxy.config.http.per_server.connection.metric_aggregate).
'''
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

Test.Summary = __doc__
import os

Test.SkipIf(Condition.CurlUsingUnixDomainSocket())

# The per hostname aggregates are derived metrics. Metrics::Derived::update_derived() runs from
# raw_stat_sync_cont (src/iocore/eventsystem/RecProcess.cc), which is scheduled every
# proxy.config.raw_stat_sync_interval_ms. That record defaults to 5000ms, which would force every
# assertion here to sleep more than five seconds; each ATS instance below shortens it so the waits
# can be short instead. The record is startup only, so it has to be set in records.yaml rather than
# adjusted at runtime. Reading before a tick lands silently compares against zeros.
_STAT_SYNC_INTERVAL_MS: int = 500

# How long to wait before reading a derived metric. Several sync periods, to absorb ET_TASK
# scheduling jitter and the traffic_ctl round trip rather than racing the tick.
_STAT_SYNC_WAIT_SECONDS: int = 2

# The records.yaml settings every ATS instance in this file needs for the waits above to hold.
_STAT_SYNC_RECORDS: dict = {
    'proxy.config.raw_stat_sync_interval_ms': _STAT_SYNC_INTERVAL_MS,
}

# NOTE: assigning to a Streams attribute REPLACES any tester already set for that stream
# (TesterSet.Assign), so every assertion after the first on the same stream must use '+=' or it
# silently discards the earlier ones. The same applies to StillRunningAfter and the other process
# state checks: they are TesterSets too.

# One microDNS server shared by every ATS instance here. All of them want the same thing, a
# wildcard answer of 127.0.0.1, and each extra process costs about five seconds when the test tears
# down, so this file uses a single server rather than one per test class.
_dns = Test.MakeDNServer("dns", default='127.0.0.1')
_dns_started: bool = False


def _use_shared_dns(tr) -> None:
    """Make the shared nameserver available to a TestRun.

    Only the first run may start it: StartBefore is tracked per TestRun, so asking twice would try
    to start an already running process. Later runs just assert it is still alive.
    """
    global _dns_started

    if _dns_started:
        # '+=': StillRunningAfter is a TesterSet like Streams, so '=' would discard any
        # process check the caller has already set on this run.
        tr.StillRunningAfter += _dns
    else:
        tr.Processes.Default.StartBefore(_dns)
        _dns_started = True


class PerServerConnectionMaxTest:
    """Define an object to test our max origin connection behavior."""

    _replay_file: str = 'slow_servers.replay.yaml'
    _origin_max_connections: int = 3

    def __init__(self) -> None:
        """Configure the test processes in preparation for the TestRun."""
        self._configure_dns()
        self._configure_server()
        self._configure_trafficserver()

    def _configure_dns(self) -> None:
        """Configure a nameserver for the test."""
        self._dns = _dns

    def _configure_server(self) -> None:
        """Configure the server to be used in the test."""
        self._server = Test.MakeVerifierServerProcess('server', self._replay_file)

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        self._ts = Test.MakeATSProcess("ts1")
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        self._ts.Disk.records_config.update(
            {
                **_STAT_SYNC_RECORDS,
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|conn_track',
                'proxy.config.http.per_server.connection.max': self._origin_max_connections,
                # The match here is 'port', which has no hostname aggregate, so the per group
                # metrics themselves are what gets checked below. That is what the default
                # metric_aggregate of 0 publishes, so only metric_enabled is needed.
                'proxy.config.http.per_server.connection.metric_enabled': 1,
                'proxy.config.http.per_server.connection.metric_prefix': 'foo',
                'proxy.config.http.per_server.connection.match': 'port',
            }
        )
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            f'WARNING:.*too many connections:.*limit={self._origin_max_connections}',
            'Verify the user is warned about the connection limit being hit.',
        )

    def _test_metrics(self) -> None:
        """Use traffic_ctl to test metrics."""
        group_name = f'foo.127.0.0.1:{self._server.Variables.http_port}'

        tr = Test.AddTestRun("Check connection metrics")
        # The per group metrics are published by mirroring the hidden ones through a derived
        # metric, so a sync tick has to pass before they carry a value.
        tr.Processes.Default.Command = f'sleep {_STAT_SYNC_WAIT_SECONDS}; traffic_ctl metric match per_server'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.TimeOut = _STAT_SYNC_WAIT_SECONDS + 30
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.{group_name} 4', 'incorrect statistic return, or possible error.'
        )
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            'INVALID_INCOMING_DATA', 'The metric query must not be rejected.'
        )
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'per_server.blocked_connection.{group_name} 1', 'incorrect statistic return, or possible error.'
        )

        # A 'port' match has one group per address:port and no hostname, so no aggregate should be
        # registered for it at all.
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            'per_server.current_connection_max.', 'A non-"both" match type must not register a hostname aggregate.'
        )

    def run(self) -> None:
        """Configure the TestRun."""
        tr = Test.AddTestRun('Verify we enforce proxy.config.http.per_server.connection.max')
        _use_shared_dns(tr)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess('client', self._replay_file, http_ports=[self._ts.Variables.port])

        self._test_metrics()


class ConnectMethodTest:
    """Test our max origin connection behavior with CONNECT traffic.

    Also covers the two aggregate-publishing modes of
    proxy.config.http.per_server.connection.metric_aggregate:
      - 2 (AGGREGATE_ONLY): only the per hostname aggregate is published; the per group metrics
        stay hidden and are visible only with --include-hidden.
      - 1 (AGGREGATE_GROUP): the per hostname aggregate is published, and the per group metrics
        are also mirrored into the published store.

    The match here defaults to 'both' and there is exactly one group for this hostname, so the
    aggregate is a trivial sum over that single group. MultiGroupAggregateTest below covers the
    case where an aggregate genuinely spans more than one group.
    """

    _process_counter: int = 0
    _client_counter: int = 0

    def __init__(self, max_conn, metric_aggregate=2) -> None:
        """Configure the server processes in preparation for the TestRun."""
        self._metric_aggregate = metric_aggregate
        self._configure_dns()
        self._configure_origin_server()
        self._configure_trafficserver(max_conn, metric_aggregate)
        ConnectMethodTest._process_counter += 1

    def _configure_dns(self) -> None:
        """Configure a nameserver for the test."""
        self._dns = _dns

    def _configure_origin_server(self) -> None:
        """Configure the httpbin origin server."""
        self._server = Test.MakeHttpBinServer(f"server_{ConnectMethodTest._process_counter}")

    def _configure_trafficserver(self, max_conn, metric_aggregate) -> None:
        self._ts = Test.MakeATSProcess(f"ts2_{max_conn}_{metric_aggregate}")

        self._ts.Disk.records_config.update(
            {
                **_STAT_SYNC_RECORDS,
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|hostdb|conn_track',
                'proxy.config.http.server_ports': f"{self._ts.Variables.port} {self._ts.Variables.uds_path}",
                'proxy.config.http.connect_ports': f"{self._server.Variables.Port}",
                'proxy.config.http.per_server.connection.metric_enabled': 1,
                'proxy.config.http.per_server.connection.metric_aggregate': metric_aggregate,
                'proxy.config.http.per_server.connection.max': max_conn,
            }
        )

        self._ts.Disk.remap_config.AddLines(
            [
                f"map http://foo.com/ http://www.this.origin.com:{self._server.Variables.Port}/",
            ]
        )
        self._ts.addPrivateConnectAllowYaml()

    def _configure_client_with_slow_response(self, tr) -> 'Test.Process':
        """Configure a client to perform a CONNECT request with a slow response from the server."""
        p = tr.Processes.Process(f'slow_client_{ConnectMethodTest._client_counter}')
        ConnectMethodTest._client_counter += 1
        tr.MakeCurlCommand(f"-v --fail -s -p -x 127.0.0.1:{self._ts.Variables.port} 'http://foo.com/delay/2'", p=p, ts=self._ts)
        return p

    def _test_metrics(self, blocked) -> None:
        """Use traffic_ctl to test metrics, honoring the configured publication level."""
        host_name = 'www.this.origin.com'
        group_name = f'{host_name}.127.0.0.1:{self._server.Variables.Port}'

        tr = Test.AddTestRun("Check connection metrics")
        tr.Processes.Default.Command = f'sleep {_STAT_SYNC_WAIT_SECONDS}; traffic_ctl metric match per_server'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.TimeOut = _STAT_SYNC_WAIT_SECONDS + 30

        # The per hostname aggregate is published in both modes under test.
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.{host_name} 5', 'incorrect statistic return, or possible error.'
        )
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'per_server.blocked_connection.{host_name} {blocked}', 'incorrect statistic return, or possible error.'
        )

        if self._metric_aggregate == 1:
            # AGGREGATE_GROUP additionally mirrors the per group metrics into the published store.
            tr.Processes.Default.Streams.All += Testers.ContainsExpression(
                f'per_server.total_connection.{group_name} 5', 'The per group metric should be published at AGGREGATE_GROUP.'
            )
        else:
            # AGGREGATE_ONLY keeps the per group metrics hidden, so none of the three per group
            # names may appear in a normal query. current_connection_max is not among them: it only
            # ever exists as a hostname aggregate, never per group.
            for counter in ('current_connection', 'total_connection', 'blocked_connection'):
                tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
                    f'per_server.{counter}.{group_name} ', f'per_server.{counter}.{group_name} must stay hidden at AGGREGATE_ONLY.'
                )

        # The per group metrics must be visible with --include-hidden at either level. This is also
        # the end to end test for that traffic_ctl option.
        tr2 = Test.AddTestRun("Check hidden per group connection metrics")
        tr2.Processes.Default.Command = 'traffic_ctl metric match per_server --include-hidden'
        tr2.Processes.Default.ReturnCode = 0
        tr2.Processes.Default.Env = self._ts.Env
        # No sleep needed: the hidden per group metrics are written directly on each connection,
        # unlike the derived aggregates.
        tr2.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.{group_name} 5',
            'The per group metric should be visible with --include-hidden at any level.',
        )
        tr2.Processes.Default.Streams.All += Testers.ExcludesExpression(
            'INVALID_INCOMING_DATA', 'The --include-hidden query must not be rejected by the RPC decoder.'
        )

    def run(self, blocked, gold_file) -> None:
        """Verify per_server.connection.max with CONNECT traffic."""
        tr = Test.AddTestRun()
        _use_shared_dns(tr)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        slow0 = self._configure_client_with_slow_response(tr)
        slow1 = self._configure_client_with_slow_response(tr)
        slow2 = self._configure_client_with_slow_response(tr)

        tr.Processes.Default.StartBefore(slow0)
        tr.Processes.Default.StartBefore(slow1)
        tr.Processes.Default.StartBefore(slow2)

        # With those three slow transactions going on in the background, do a
        # couple quick transactions and make sure they both reply with a 503
        # response.
        tr.MakeCurlCommandMulti(
            f"sleep 1; {{curl}} -v --fail -s -p -x 127.0.0.1:{self._ts.Variables.port} 'http://foo.com/get'"
            f"--next -v --fail -s -p -x 127.0.0.1:{self._ts.Variables.port} 'http://foo.com/get'",
            ts=self._ts,
        )
        # Curl will have a 22 exit code if it receives a 5XX response (and we
        # expect a 503).
        tr.Processes.Default.ReturnCode = 22 if blocked else 0
        tr.Processes.Default.Streams.stderr = gold_file
        tr.Processes.Default.TimeOut = 3

        self._test_metrics(blocked)


class MultiGroupAggregateTest:
    """Verify a per hostname aggregate that genuinely spans more than one group.

    The other tests here resolve a hostname to a single 127.0.0.1:port, so their "aggregate" is
    trivially a set of one. Here two remap rules point at the same hostname ('multi.origin.com')
    on two different origin ports, so under match 'both' the connection tracker creates two
    distinct groups sharing one host aggregate. The two groups are given different concurrency so
    the SUM and the MAX are distinguishable from each other.

    current_connection and current_connection_max are instantaneous gauges recomputed from the live
    per group values every ~5s, so they rise and fall with traffic rather than remembering a peak.
    Observing a non-zero value therefore requires holding connections open across a sync tick. The
    most robust assertion, and the one that actually distinguishes this instantaneous behavior from
    a monotone peak, is that both gauges return to 0 once traffic drains and another tick passes.
    """

    _process_counter: int = 0
    _client_counter: int = 0

    # Concurrent slow requests per group. Deliberately different so SUM (5) and MAX (3) differ.
    _group_a_concurrency: int = 2
    _group_b_concurrency: int = 3

    # How long each request holds its connection open. Must comfortably exceed
    # _STAT_SYNC_WAIT_SECONDS so a sync tick is guaranteed to land while the connections are still
    # open. Well under the 10 second cap httpbin puts on /delay/<n>, so no clamping applies.
    _hold_seconds: int = 6

    def __init__(self) -> None:
        """Configure the test processes in preparation for the TestRun."""
        self._configure_dns()
        self._configure_origin_servers()
        self._configure_trafficserver()
        MultiGroupAggregateTest._process_counter += 1

    def _configure_dns(self) -> None:
        """Configure a nameserver for the test."""
        self._dns = _dns

    def _configure_origin_servers(self) -> None:
        """Configure the two httpbin origins which stand in for two groups of one hostname."""
        self._server_a = Test.MakeHttpBinServer(f"magg_server_a_{MultiGroupAggregateTest._process_counter}")
        self._server_b = Test.MakeHttpBinServer(f"magg_server_b_{MultiGroupAggregateTest._process_counter}")

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server with two remap rules to the same hostname on different ports."""
        self._ts = Test.MakeATSProcess(f"magg_ts_{MultiGroupAggregateTest._process_counter}")
        self._ts.Disk.records_config.update(
            {
                **_STAT_SYNC_RECORDS,
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|hostdb|conn_track',
                'proxy.config.http.per_server.connection.metric_enabled': 1,
                # Aggregates only: the per group metrics stay hidden, which is what this test is
                # about reading through the aggregate.
                'proxy.config.http.per_server.connection.metric_aggregate': 2,
                'proxy.config.http.per_server.connection.match': 'both',
            }
        )
        self._ts.Disk.remap_config.AddLines(
            [
                f"map http://multi.origin.com/a/ http://multi.origin.com:{self._server_a.Variables.Port}/",
                f"map http://multi.origin.com/b/ http://multi.origin.com:{self._server_b.Variables.Port}/",
            ]
        )

    def _make_slow_client(self, tr, path) -> 'Test.Process':
        """Configure a client which makes a slow request through one of the two remapped groups."""
        p = tr.Processes.Process(f'magg_client_{MultiGroupAggregateTest._client_counter}')
        MultiGroupAggregateTest._client_counter += 1
        tr.MakeCurlCommand(
            f"-v --fail -s -x 127.0.0.1:{self._ts.Variables.port} "
            f"'http://multi.origin.com/{path}/delay/{MultiGroupAggregateTest._hold_seconds}'",
            p=p,
            ts=self._ts,
        )
        return p

    def _test_metrics_while_held(self) -> None:
        """While the slow requests are still in flight, verify the live gauges reflect them."""
        total = MultiGroupAggregateTest._group_a_concurrency + MultiGroupAggregateTest._group_b_concurrency
        group_max = max(MultiGroupAggregateTest._group_a_concurrency, MultiGroupAggregateTest._group_b_concurrency)

        tr = Test.AddTestRun("Check the host aggregate spans both groups while connections are held open")
        tr.Processes.Default.Command = f'sleep {_STAT_SYNC_WAIT_SECONDS}; traffic_ctl metric match per_server'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.TimeOut = _STAT_SYNC_WAIT_SECONDS + 30
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.multi.origin.com {total}',
            'The host aggregate total_connection should be the SUM across both groups '
            f'({MultiGroupAggregateTest._group_a_concurrency} + {MultiGroupAggregateTest._group_b_concurrency}).',
        )
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'per_server.current_connection.multi.origin.com {total}',
            'While held open, the host aggregate current_connection should be the SUM of the '
            'currently open connections across both groups.',
        )
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'per_server.current_connection_max.multi.origin.com {group_max}',
            'While held open, current_connection_max should be the largest single group current '
            'count (MAX), not the sum across the two groups.',
        )

    def _test_metrics_after_drain(self) -> None:
        """After traffic drains and a further sync tick passes, both live gauges must read 0.

        This validates the behavior the design exists to provide: an instantaneous gauge, unlike a
        monotone peak, comes back down.
        """
        tr = Test.AddTestRun("Check the host aggregate drains back to 0 after traffic stops")
        # The slow requests are already _STAT_SYNC_WAIT_SECONDS old by now; wait for the rest of
        # their hold time and then for another sync tick to observe the drop to 0.
        wait = max(0, MultiGroupAggregateTest._hold_seconds - _STAT_SYNC_WAIT_SECONDS) + _STAT_SYNC_WAIT_SECONDS
        tr.Processes.Default.Command = f'sleep {wait}; traffic_ctl metric match per_server'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.TimeOut = wait + 30
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            'per_server.current_connection.multi.origin.com 0',
            'Once all connections close, the host aggregate current_connection must drain to 0.',
        )
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'per_server.current_connection_max.multi.origin.com 0',
            'Once all connections close, current_connection_max must also come back down to 0: it '
            'is a live gauge, not a monotone peak.',
        )

    def run(self) -> None:
        """Drive concurrent traffic through both groups, then check the aggregate metrics."""
        tr = Test.AddTestRun()
        _use_shared_dns(tr)
        tr.Processes.Default.StartBefore(self._server_a)
        tr.Processes.Default.StartBefore(self._server_b)
        tr.Processes.Default.StartBefore(self._ts)

        clients = [self._make_slow_client(tr, 'a') for _ in range(MultiGroupAggregateTest._group_a_concurrency)]
        clients += [self._make_slow_client(tr, 'b') for _ in range(MultiGroupAggregateTest._group_b_concurrency)]
        for p in clients:
            tr.Processes.Default.StartBefore(p)

        # Let the slow requests connect and overlap before checking anything; they stay open for
        # _hold_seconds from about this point.
        tr.Processes.Default.Command = 'sleep 1'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 30

        self._test_metrics_while_held()
        self._test_metrics_after_drain()


class MetricOverrideTest:
    """Verify proxy.config.http.per_server.connection.metric_enabled is overridable per remap rule.

    Metrics are enabled globally and one of the two remap rules turns them off with
    conf_remap. The two rules point at different origin ports and the match is 'port', so each gets
    its own group and the two decisions cannot influence each other.
    """

    def __init__(self) -> None:
        """Configure the test processes in preparation for the TestRun."""
        self._dns = _dns
        self._server_on = Test.MakeHttpBinServer("server_metric_on")
        self._server_off = Test.MakeHttpBinServer("server_metric_off")
        self._configure_trafficserver()

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        self._ts = Test.MakeATSProcess("ts_metric_override")
        self._ts.Disk.records_config.update(
            {
                **_STAT_SYNC_RECORDS,
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|conn_track',
                # Enabled globally; the second remap rule below opts out.
                'proxy.config.http.per_server.connection.metric_enabled': 1,
                'proxy.config.http.per_server.connection.match': 'port',
            }
        )
        self._ts.Disk.remap_config.AddLines(
            [
                f'map http://metric-on.com/ http://127.0.0.1:{self._server_on.Variables.Port}/',
                f'map http://metric-off.com/ http://127.0.0.1:{self._server_off.Variables.Port}/'
                ' @plugin=conf_remap.so'
                ' @pparam=proxy.config.http.per_server.connection.metric_enabled=0',
            ]
        )

    def _test_metrics(self) -> None:
        """Use traffic_ctl to verify which per server metrics exist."""
        on_group = f'127.0.0.1:{self._server_on.Variables.Port}'
        off_group = f'127.0.0.1:{self._server_off.Variables.Port}'

        tr = Test.AddTestRun("Check that only the non-overridden remap has per server metrics")
        tr.Processes.Default.Command = f'sleep {_STAT_SYNC_WAIT_SECONDS}; traffic_ctl metric match per_server'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.TimeOut = _STAT_SYNC_WAIT_SECONDS + 30
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.{on_group} 1', 'The remap with metrics enabled should have per server metrics.'
        )
        # The group for the overridden remap must not exist at all, hidden or otherwise, so this
        # also holds with --include-hidden below.
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            f'per_server.total_connection.{off_group}', 'The remap with metrics disabled should have no per server metrics.'
        )

        tr2 = Test.AddTestRun("The overridden remap has no hidden per server metrics either")
        tr2.Processes.Default.Command = 'traffic_ctl metric match per_server --include-hidden'
        tr2.Processes.Default.ReturnCode = 0
        tr2.Processes.Default.Env = self._ts.Env
        tr2.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.{on_group} 1', 'The enabled remap group should be present in the hidden store.'
        )
        tr2.Processes.Default.Streams.All += Testers.ExcludesExpression(
            f'per_server.total_connection.{off_group}', 'No group should be created at all for the overridden remap.'
        )

    def run(self) -> None:
        """Configure the TestRun."""
        tr = Test.AddTestRun('Verify metric_enabled is overridable per remap rule')
        _use_shared_dns(tr)
        tr.Processes.Default.StartBefore(self._server_on)
        tr.Processes.Default.StartBefore(self._server_off)
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommandMulti(
            f"{{curl}} -v -s -H 'Host: metric-on.com' http://127.0.0.1:{self._ts.Variables.port}/get"
            f" --next -v -s -H 'Host: metric-off.com' http://127.0.0.1:{self._ts.Variables.port}/get"
        )
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 30
        tr.StillRunningAfter += self._ts

        self._test_metrics()


class AggregateOnlyWithoutHostAggregateTest:
    """Verify metric_aggregate 2 still publishes per group metrics when there is no aggregate.

    metric_aggregate 2 (AGGREGATE_ONLY) normally leaves the per group metrics hidden and publishes
    only the per hostname aggregate. That aggregate exists only under match 'both', which is the
    only match type with more than one group per hostname (Group::host_metric_name returns empty
    for the others). With match 'port' there is therefore nothing for the aggregate to stand in
    for, so the per group metrics have to be published regardless, or level 2 would report nothing
    at all for this group.

    Every other test in this file that sets metric_aggregate 2 uses match 'both', so without this
    case a regression that dropped the fallback would leave the suite green.
    """

    def __init__(self) -> None:
        """Configure the test processes in preparation for the TestRun."""
        self._dns = _dns
        self._server = Test.MakeHttpBinServer("agg_only_no_host_server")
        self._configure_trafficserver()

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server for aggregates only against a match type with no aggregate."""
        self._ts = Test.MakeATSProcess("ts_agg_only_no_host")
        self._ts.Disk.records_config.update(
            {
                **_STAT_SYNC_RECORDS,
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|conn_track',
                'proxy.config.http.per_server.connection.metric_enabled': 1,
                'proxy.config.http.per_server.connection.metric_aggregate': 2,
                # 'port' has no per hostname aggregate, which is the point of this test.
                'proxy.config.http.per_server.connection.match': 'port',
            }
        )
        self._ts.Disk.remap_config.AddLine(f'map http://agg-only.com/ http://127.0.0.1:{self._server.Variables.Port}/')

    def _test_metrics(self) -> None:
        """Verify the per group metrics are published despite metric_aggregate 2."""
        group = f'127.0.0.1:{self._server.Variables.Port}'

        tr = Test.AddTestRun("Check the per group metrics are published when no aggregate exists")
        tr.Processes.Default.Command = f'sleep {_STAT_SYNC_WAIT_SECONDS}; traffic_ctl metric match per_server'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.TimeOut = _STAT_SYNC_WAIT_SECONDS + 30
        # Published, not just hidden: this query does not pass --include-hidden.
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            f'per_server.total_connection.{group} 1',
            'At metric_aggregate 2 with a match type that has no aggregate, the per group metric must still be published.',
        )
        # The hostname never appears in a metric name under match 'port', so its absence confirms
        # the published metric came from the per group fallback and not from an aggregate.
        tr.Processes.Default.Streams.All += Testers.ExcludesExpression(
            'per_server.total_connection.agg-only.com', 'No per hostname aggregate should exist for match "port".'
        )

    def run(self) -> None:
        """Drive one request through the origin, then check the metrics."""
        tr = Test.AddTestRun('Verify metric_aggregate 2 falls back to per group metrics')
        _use_shared_dns(tr)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.MakeCurlCommand(f"-v -s -H 'Host: agg-only.com' http://127.0.0.1:{self._ts.Variables.port}/get", ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.TimeOut = 30
        tr.StillRunningAfter += self._ts

        self._test_metrics()


PerServerConnectionMaxTest().run()
ConnectMethodTest(3, metric_aggregate=2).run(blocked=2, gold_file="gold/two_503_congested.gold")
ConnectMethodTest(0, metric_aggregate=1).run(blocked=0, gold_file="gold/two_200_ok.gold")
MultiGroupAggregateTest().run()
MetricOverrideTest().run()
AggregateOnlyWithoutHostAggregateTest().run()
