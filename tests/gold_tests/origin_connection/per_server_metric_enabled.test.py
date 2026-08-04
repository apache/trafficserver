'''
Verify per_server connection tracking stays accurate when
proxy.config.http.per_server.connection.metric_enabled is set.
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

Test.SkipIf(Condition.CurlUsingUnixDomainSocket())


class PerServerMetricEnabledTest:
    """Verify that enabling the per_server metrics does not break the group connection count.

    Enabling proxy.config.http.per_server.connection.metric_enabled used to
    make the metric the authoritative connection count, leaving the group's
    internal counter at zero. That made every pooled origin session look like
    it was at or below proxy.config.http.per_server.connection.min, so
    keep-alive origin connections were never reaped on inactivity timeout.
    """

    _replay_file: str = 'per_server_metric_enabled.replay.yaml'
    _keep_alive_timeout: int = 2

    def __init__(self) -> None:
        """Configure the test processes in preparation for the TestRun."""
        self._configure_server()
        self._configure_trafficserver()

    def _configure_server(self) -> None:
        """Configure the origin server to be used in the test."""
        self._server = Test.MakeVerifierServerProcess('metric_enabled_server', self._replay_file)

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        self._ts = Test.MakeATSProcess("ts_metric_enabled")
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http_ss|conn_track',
                'proxy.config.http.per_server.connection.metric_enabled': 1,
                'proxy.config.http.per_server.connection.metric_prefix': 'bar',
                'proxy.config.http.per_server.connection.match': 'port',
                # No minimum number of keep alive origin connections: the pooled
                # connection should be closed once it times out.
                'proxy.config.http.per_server.connection.min': 0,
                'proxy.config.http.keep_alive_no_activity_timeout_out': self._keep_alive_timeout,
                'proxy.config.http.server_session_sharing.pool': 'global',
            })
        # The connection count should never be decremented below zero.
        self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            'Number of tracked connections should be greater than or equal to zero',
            'Verify the group connection count is not double decremented.')

    def _test_connection_is_reaped(self) -> None:
        """Verify the idle origin connection is closed once it times out."""
        tr = Test.AddTestRun("Verify the idle keep-alive origin connection is reaped")
        tr.Processes.Default.Command = (
            f'sleep {self._keep_alive_timeout * 3}; '
            'traffic_ctl metric get proxy.process.http.current_server_connections; '
            'traffic_ctl metric match per_server')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'proxy.process.http.current_server_connections 0',
            'The idle origin connection should have been closed by the keep-alive timeout.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'per_server.current_connection.bar.127.0.0.1:{self._server.Variables.http_port} 0',
            'The per_server connection gauge should have been decremented back to zero.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f'per_server.total_connection.bar.127.0.0.1:{self._server.Variables.http_port} 1',
            'A single origin connection should have been tracked.')

    def _test_tracker_info(self) -> None:
        """Verify the JSONRPC connection tracker report agrees with the metrics."""
        tr = Test.AddTestRun("Verify the connection tracker report")
        tr.Processes.Default.Command = "traffic_ctl rpc invoke get_connection_tracker_info -p 'table: outbound' -f json"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        # Once the connection is released the group count drops to zero and the
        # group is removed from the table, so either the table is empty or the
        # remaining group reports no current connections.
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            r'"(count|current)":\s*"?0"?', 'The tracker should report no current outbound connections.')

    def run(self) -> None:
        """Configure the TestRuns."""
        tr = Test.AddTestRun('Perform a transaction that leaves a pooled origin connection')
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess('metric_enabled_client', self._replay_file, http_ports=[self._ts.Variables.port])

        self._test_connection_is_reaped()
        self._test_tracker_info()


PerServerMetricEnabledTest().run()
