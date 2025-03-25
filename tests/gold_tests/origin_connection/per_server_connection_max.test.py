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

    def _configure_dns(self) -> None:
        """Configure a nameserver for the test."""
        self._dns = Test.MakeDNServer("dns1", default='127.0.0.1')

    def _configure_server(self) -> None:
        """Configure the server to be used in the test."""
        self._server = Test.MakeVerifierServerProcess('server1', self._replay_file)

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        self._ts = Test.MakeATSProcess("ts1")
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.per_server.connection.max': self._origin_max_connections,
            })
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            f'WARNING:.*too many connections:.*limit={self._origin_max_connections}',
            'Verify the user is warned about the connection limit being hit.')

    def run(self) -> None:
        """Configure the TestRun."""
        tr = Test.AddTestRun('Verify we enforce proxy.config.http.per_server.connection.max')
        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

        tr.AddVerifierClientProcess('client', self._replay_file, http_ports=[self._ts.Variables.port])


class ConnectMethodTest:
    """Test our max origin connection behavior with CONNECT traffic."""

    _client_counter: int = 0
    _origin_max_connections: int = 3

    def __init__(self) -> None:
        """Configure the server processes in preparation for the TestRun."""
        self._configure_dns()
        self._configure_origin_server()
        self._configure_trafficserver()

    def _configure_dns(self) -> None:
        """Configure a nameserver for the test."""
        self._dns = Test.MakeDNServer("dns2", default='127.0.0.1')

    def _configure_origin_server(self) -> None:
        """Configure the httpbin origin server."""
        self._server = Test.MakeHttpBinServer("server2")

    def _configure_trafficserver(self) -> None:
        self._ts = Test.MakeATSProcess("ts2")

        self._ts.Disk.records_config.update(
            {
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|dns|hostdb',
                'proxy.config.http.server_ports': f"{self._ts.Variables.port}",
                'proxy.config.http.connect_ports': f"{self._server.Variables.Port}",
                'proxy.config.http.per_server.connection.max': self._origin_max_connections,
            })

        self._ts.Disk.remap_config.AddLines([
            f"map http://foo.com/ http://www.this.origin.com:{self._server.Variables.Port}/",
        ])

    def _configure_client_with_slow_response(self, tr) -> 'Test.Process':
        """Configure a client to perform a CONNECT request with a slow response from the server."""
        p = tr.Processes.Process(f'slow_client_{self._client_counter}')
        self._client_counter += 1
        tr.MakeCurlCommand(f"-v --fail -s -p -x 127.0.0.1:{self._ts.Variables.port} 'http://foo.com/delay/2'", p=p)
        return p

    def run(self) -> None:
        """Verify per_server.connection.max with CONNECT traffic."""
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self._dns)
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
            f"--next -v --fail -s -p -x 127.0.0.1:{self._ts.Variables.port} 'http://foo.com/get'")
        # Curl will have a 22 exit code if it receives a 5XX response (and we
        # expect a 503).
        tr.Processes.Default.ReturnCode = 22
        tr.Processes.Default.Streams.stderr = "gold/two_503_congested.gold"
        tr.Processes.Default.TimeOut = 3


PerServerConnectionMaxTest().run()
ConnectMethodTest().run()
