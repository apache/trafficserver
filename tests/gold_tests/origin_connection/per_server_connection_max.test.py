'''
Verify the behavior of proxy.config.http.per_server.connection.max.
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


class PerServerConnectionMaxTest:
    """Define an object to test our max origin connection behavior."""

    _replay_file: str = 'slow_servers.replay.yaml'
    _origin_max_connections: int = 3

    def __init__(self) -> None:
        """Configure the test processes in preparation for the TestRun."""
        self._configure_dns()
        self._configure_server()
        self._configure_trafficserver()

    def _configure_dns(self):
        """Configure a nameserver for the test."""
        self._nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

    def _configure_server(self) -> None:
        """Configure the server to be used in the test."""
        self._server = Test.MakeVerifierServerProcess('server', self._replay_file)

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        self._ts = Test.MakeATSProcess("ts")
        self._ts.Disk.remap_config.AddLine(
            f'map / http://127.0.0.1:{self._server.Variables.http_port}'
        )
        self._ts.Disk.records_config.update({
            'proxy.config.dns.nameservers': f"127.0.0.1:{self._nameserver.Variables.Port}",
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http',
            'proxy.config.http.per_server.connection.max': self._origin_max_connections,
        })
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            f'WARNING:.*too many connections:.*limit={self._origin_max_connections}',
            'Verify the user is warned about the connection limit being hit.')

    def run(self) -> None:
        """Configure the TestRun."""
        tr = Test.AddTestRun(
            'Verify we enforce proxy.config.http.per_server.connection.max')
        tr.Processes.Default.StartBefore(self._nameserver)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess(
            'client',
            self._replay_file,
            http_ports=[self._ts.Variables.port])


PerServerConnectionMaxTest().run()
