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
import os

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

    _process_counter: int = 0
    _max_client_connections: int = 3
    _protocol_to_replay_file = {
        Protocol.HTTP: 'http_slow_origins.replay.yaml',
        Protocol.HTTPS: 'https_slow_origins.replay.yaml',
        Protocol.HTTP2: 'http2_slow_origins.replay.yaml',
    }

    def __init__(self, protocol: int, exempt_list: str = '', exempt_list_file: str = '', exempt_list_applies: bool = False) -> None:
        """Configure the test processes in preparation for the TestRun.

        :param protocol: The protocol to test.
        :param exempt_list: A comma-separated string of IP addresses or ranges to exempt.
          The default empty string implies that no exempt list will be configured.
        :param exempt_list_file: A file containing a list of IP addresses or ranges to exempt.
          The default empty string implies that no exempt list will be configured.
        :param exempt_list_applies: If True, the exempt list is assumed to exempt
          the test connections. Thus the per client max connections is expected
          not to be enforced for the connections.
        """
        self._process_counter = PerClientConnectionMaxTest._process_counter
        PerClientConnectionMaxTest._process_counter += 1
        self._protocol = protocol
        protocol_string = Protocol.to_str(protocol)
        self._replay_file = self._protocol_to_replay_file[protocol]
        self._exempt_list = exempt_list
        self._exempt_list_file = exempt_list_file
        self._exempt_list_applies = exempt_list_applies

        exempt_list_description = 'exempted' if exempt_list_applies else 'not exempted'
        exempt_description = 'no exempt list'
        if exempt_list:
            exempt_description = 'exempt list string'
        elif exempt_list_file:
            exempt_description = 'exempt list file'
        tr = Test.AddTestRun(
            f'proxy.config.net.per_client.connection.max: {protocol_string}, '
            f'{exempt_description}: {exempt_list_description}')
        self._configure_dns(tr)
        self._configure_server(tr)
        self._configure_trafficserver()
        self._configure_client(tr)
        self._verify_metrics()

    def _configure_dns(self, tr: 'TestRun') -> None:
        """Configure a nameserver for the test.

        :param tr: The TestRun to add the nameserver to.
        """
        name = f'dns{self._process_counter}'
        self._dns = tr.MakeDNServer(name, default='127.0.0.1')

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the server to be used in the test.

        :param tr: The TestRun to add the server to.
        """
        name = f'server{self._process_counter}'
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)
        self._server.Streams.All += Testers.ContainsExpression("first-request", "Verify the first request was received.")
        self._server.Streams.All += Testers.ContainsExpression("second-request", "Verify the second request was received.")
        self._server.Streams.All += Testers.ContainsExpression("third-request", "Verify the third request was received.")
        self._server.Streams.All += Testers.ContainsExpression("fifth-request", "Verify the fifth request was received.")

        if self._exempt_list_applies:
            # The fourth request should be allowed due to the exempt_list.
            self._server.Streams.All += Testers.ContainsExpression("fourth-request", "Verify the fourth request was received.")
        else:
            # The fourth request should be blocked due to too many connections.
            self._server.Streams.All += Testers.ExcludesExpression("fourth-request", "Verify the fourth request was not received.")

    def _configure_trafficserver(self) -> None:
        """Configure Traffic Server to be used in the test."""
        # Associate ATS with the Test so that metrics can be verified.
        name = f'ts{self._process_counter}'
        self._ts = Test.MakeATSProcess(name, enable_cache=False, enable_tls=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
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
                'proxy.config.diags.debug.tags': 'socket|http|net_queue|iocore_net|conn_track|cripts',
                'proxy.config.net.per_client.max_connections_in': self._max_client_connections,
                # Disable keep-alive so we close the client connections when the
                # transactions are done. This allows us to verify cleanup is working
                # per the ConnectionTracker metrics.
                'proxy.config.http.keep_alive_enabled_in': 0,
            })
        if self._exempt_list_file:
            exempt_list_absolute = os.path.join(self._ts.Variables.CONFIGDIR, os.path.basename(self._exempt_list_file))
            self._ts.Setup.Copy(self._exempt_list_file, exempt_list_absolute)
            self._ts.Disk.plugin_config.AddLine(f'connection_exempt_list.so {exempt_list_absolute}')
        elif self._exempt_list:
            self._ts.Disk.records_config.update({
                'proxy.config.http.per_client.connection.exempt_list': self._exempt_list,
            })
        if self._exempt_list_applies:
            self._ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                f'WARNING:.*too many connections:', 'Connections should not be throttled due to the exempt list.')
        else:
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                f'WARNING:.*too many connections:.*limit={self._max_client_connections}',
                'Verify the user is warned about the connection limit being hit.')

    def _configure_client(self, tr: 'TestRun') -> None:
        """Configure the TestRun.

        :param tr: The TestRun to add the client to.
        """
        name = f'client{self._process_counter}'
        p = tr.AddVerifierClientProcess(
            name,
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            https_ports=[self._ts.Variables.ssl_port],
            run_parallel=True)

        p.StartBefore(self._dns)
        p.StartBefore(self._server)
        p.StartBefore(self._ts)

        p.Streams.All += Testers.ContainsExpression("first-request", "Verify the first request was received.")
        p.Streams.All += Testers.ContainsExpression("second-request", "Verify the second request was received.")
        p.Streams.All += Testers.ContainsExpression("third-request", "Verify the third request was received.")
        p.Streams.All += Testers.ContainsExpression("fifth-request", "Verify the fifth request was received.")
        if self._exempt_list_applies:
            p.ReturnCode = 0
            p.Streams.All += Testers.ContainsExpression("fourth-request", "Verify the fourth request was received.")
        else:
            # Because the fourth connection will be aborted, the client will have a
            # non-zero return code.
            p.ReturnCode = 1
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
            'proxy.process.net.per_client.connections_exempt_in '
            'proxy.process.net.connection_tracker_table_size')
        tr.Processes.Default.ReturnCode = 0
        if self._exempt_list_applies:
            tr.Processes.Default.Streams.All += Testers.ContainsExpression(
                'proxy.process.net.per_client.connections_throttled_in 0', 'Verify no connections were recorded as throttled.')
            tr.Processes.Default.Streams.All += Testers.ContainsExpression(
                'proxy.process.net.per_client.connections_exempt_in 5',
                'Verify that the connections were all recorded as exempted.')
        else:
            tr.Processes.Default.Streams.All += Testers.ContainsExpression(
                'proxy.process.net.per_client.connections_throttled_in 1', 'Verify the connection was recorded as throttled.')
            tr.Processes.Default.Streams.All += Testers.ContainsExpression(
                'proxy.process.net.per_client.connections_exempt_in 0', 'Verify no connections were recorded as exempt.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'proxy.process.net.connection_tracker_table_size 0', 'Verify the table was cleaned up correctly.')


PerClientConnectionMaxTest(Protocol.HTTP)
PerClientConnectionMaxTest(Protocol.HTTPS)
PerClientConnectionMaxTest(Protocol.HTTP2)

PerClientConnectionMaxTest(Protocol.HTTP, exempt_list='127.0.0.1,::1', exempt_list_applies=True)
PerClientConnectionMaxTest(Protocol.HTTPS, exempt_list='1.2.3.4,5.6.0.0/16', exempt_list_applies=False)
PerClientConnectionMaxTest(Protocol.HTTP2, exempt_list='0/0,::/0', exempt_list_applies=True)

PerClientConnectionMaxTest(Protocol.HTTP, exempt_list_file='exempt_lists/exempt_localhost.yaml', exempt_list_applies=True)
PerClientConnectionMaxTest(Protocol.HTTP, exempt_list_file='exempt_lists/no_localhost.yaml', exempt_list_applies=False)
PerClientConnectionMaxTest(Protocol.HTTP, exempt_list_file='exempt_lists/exempt_all.yaml', exempt_list_applies=True)
