'''
Verify the behavior of proxy.config.net.per_client.connection.max.
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

from enum import Enum

Test.Summary = __doc__


class Protocol(Enum):
    HTTP = 1
    HTTPS = 2
    HTTP2 = 3

    @classmethod
    def to_str(cls, protocol: int) -> str:
        """Convert the protocol to a string.

        :param protocol: The protocol to convert.
        :return: The string representation of the protocol.
        """
        if protocol == cls.HTTP:
            return 'http'
        elif protocol == cls.HTTPS:
            return 'https'
        elif protocol == cls.HTTP2:
            return 'http2'
        else:
            raise ValueError(f'Unknown protocol: {protocol}')


class PerClientConnectionMaxTest:
    """Define an object to test our max client connection behavior."""

    _dns_counter: int = 0
    _server_counter: int = 0
    _ts_counter: int = 0
    _client_counter: int = 0
    _max_client_connections: int = 3
    _protocol_to_replay_file = {
        Protocol.HTTP: 'http_slow_origins.replay.yaml',
        Protocol.HTTPS: 'https_slow_origins.replay.yaml',
        Protocol.HTTP2: 'http2_slow_origins.replay.yaml',
    }

    def __init__(self, protocol: int) -> None:
        """Configure the test processes in preparation for the TestRun.

        :param protocol: The protocol to test.
        """
        self._protocol = protocol
        protocol_string = Protocol.to_str(protocol)
        self._replay_file = self._protocol_to_replay_file[protocol]
        tr = Test.AddTestRun(f'proxy.config.net.per_client.connection.max: {protocol_string}')
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_trafficserver()
        self._configure_client(tr)
        self._verify_metrics()

    def _configure_dns(self, tr: 'TestRun') -> None:
        """Configure a nameserver for the test.

        :param tr: The TestRun to add the nameserver to.
        """
        name = f'dns{PerClientConnectionMaxTest._dns_counter}'
        self._dns = tr.MakeDNServer(name, default='127.0.0.1')
        PerClientConnectionMaxTest._dns_counter += 1

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the server to be used in the test.

        :param tr: The TestRun to add the server to.
        """
        name = f'server{PerClientConnectionMaxTest._server_counter}'
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)
        PerClientConnectionMaxTest._server_counter += 1
        self._server.Streams.All += Testers.ContainsExpression(
            "first-request", "Verify the first request should have been received.")
        self._server.Streams.All += Testers.ContainsExpression(
            "second-request", "Verify the second request should have been received.")
        self._server.Streams.All += Testers.ContainsExpression(
            "third-request", "Verify the third request should have been received.")
        self._server.Streams.All += Testers.ContainsExpression(
            "fifth-request", "Verify the fifth request should have been received.")

        # The fourth request should be blocked due to too many connections.
        self._server.Streams.All += Testers.ExcludesExpression(
            "fourth-request", "Verify the fourth request should not be received.")

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        # Associate ATS with the Test so that metrics can be verified.
        name = f'ts{PerClientConnectionMaxTest._ts_counter}'
        self._ts = Test.MakeATSProcess(name, enable_cache=False, enable_tls=True)
        PerClientConnectionMaxTest._ts_counter += 1
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        if self._protocol == Protocol.HTTP:
            server_port = self._server.Variables.http_port
            scheme = 'http'
        else:
            server_port = self._server.Variables.https_port
            scheme = 'https'
        self._ts.Disk.remap_config.AddLine(f'map / {scheme}://127.0.0.1:{server_port}')
        self._ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'socket|http|net_queue|iocore_net|conn_track',
                'proxy.config.net.per_client.max_connections_in': self._max_client_connections,
                # Disable keep-alive so we close the client connections when the
                # transactions are done. This allows us to verify cleanup is working
                # per the ConnectionTracker metrics.
                'proxy.config.http.keep_alive_enabled_in': 0,
            })
        self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
            f'WARNING:.*too many connections:.*limit={self._max_client_connections}',
            'Verify the user is warned about the connection limit being hit.')

    def _configure_client(self, tr: 'TestRun') -> None:
        """Configure the TestRun.

        :param tr: The TestRun to add the client to.
        """
        name = f'client{PerClientConnectionMaxTest._client_counter}'
        p = tr.AddVerifierClientProcess(
            name, self._replay_file, http_ports=[self._ts.Variables.port], https_ports=[self._ts.Variables.ssl_port])
        PerClientConnectionMaxTest._client_counter += 1

        p.StartBefore(self._dns)
        p.StartBefore(self._server)
        p.StartBefore(self._ts)

        # Because the fourth connection will be aborted, the client will have a
        # non-zero return code.
        p.ReturnCode = 1
        p.Streams.All += Testers.ContainsExpression("first-request", "Verify the first request should have been received.")
        p.Streams.All += Testers.ContainsExpression("second-request", "Verify the second request should have been received.")
        p.Streams.All += Testers.ContainsExpression("third-request", "Verify the third request should have been received.")
        p.Streams.All += Testers.ContainsExpression("fifth-request", "Verify the fifth request should have been received.")
        if self._protocol == Protocol.HTTP:
            p.Streams.All += Testers.ContainsExpression(
                "The peer closed the connection while reading.",
                "A connection should be closed due to too many client connections.")
            p.Streams.All += Testers.ContainsExpression(
                "Failed HTTP/1 transaction with key: fourth-request", "The fourth request should fail.")
        else:
            p.Streams.All += Testers.ContainsExpression(
                "ECONNRESET: Connection reset by peer", "A connection should be closed due to too many client connections.")
            p.Streams.All += Testers.ExcludesExpression("fourth-request", "The fourth request should fail.")

    def _verify_metrics(self) -> None:
        """Verify the per client connection metrics."""
        tr = Test.AddTestRun("Verify the per client connection metrics.")
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.Command = (
            'traffic_ctl metric get '
            'proxy.process.net.per_client.connections_throttled_in '
            'proxy.process.net.connection_tracker_table_size')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'proxy.process.net.per_client.connections_throttled_in 1', 'Verify the per client throttled metric is correct.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'proxy.process.net.connection_tracker_table_size 0', 'Verify the table was cleaned up correctly.')


PerClientConnectionMaxTest(Protocol.HTTP)
PerClientConnectionMaxTest(Protocol.HTTPS)
PerClientConnectionMaxTest(Protocol.HTTP2)
