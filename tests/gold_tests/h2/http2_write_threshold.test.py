"""Test proxy.config.http2.write_size_threshold."""

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

import os
from ports import get_port
import sys


class TestGrpc():
    """Test proxy.config.http2.write_size_threshold and its associated timeout."""

    def __init__(self, description: str, write_threshold: int, write_timeout: int) -> None:
        """Configure a TestRun for gRPC traffic.

        :param description: The description for the test runs.
        """
        self._description = description
        tr = Test.AddTestRun(self._description)
        dns = self._configure_dns(tr)
        server = self._configure_h2_server(tr, write_timeout)
        ts = self._configure_traffic_server(tr, dns.Variables.Port, server.Variables.port, write_threshold, write_timeout)

        ts.StartBefore(dns)
        ts.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)

        tr.TimeOut = 10

        self._configure_h2_client(tr, ts.Variables.ssl_port, write_timeout)

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        """Configure a locally running MicroDNS server.

        :param tr: The TestRun with which to associate the MicroDNS server.
        :return: The MicroDNS server process.
        """
        self._dns = tr.MakeDNServer("dns", default=['127.0.0.1'])
        return self._dns

    def _configure_h2_server(self, tr: 'TestRun', write_timeout: int) -> 'Process':
        """Set up the go HTTP/2 server.

        :param tr: The TestRun with which to associate the server.
        :param write_timeout: The expected maximum amount of time frames should be delivered.
        :return: The server process.
        """
        tr.Setup.Copy('trickle_server.py')
        self._server = tr.Processes.Process('server')

        server_pem = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
        server_key = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")
        self._server.Setup.Copy(server_pem)
        self._server.Setup.Copy(server_key)

        port = get_port(self._server, 'port')
        command = (f'{sys.executable} {tr.RunDirectory}/trickle_server.py {port} '
                   f'server.pem server.key {write_timeout}')
        self._server.Command = command
        self._server.ReturnCode = 0
        self._server.Ready = When.PortOpen(port)
        return self._server

    def _configure_traffic_server(
            self, tr: 'TestRun', dns_port: int, server_port: int, write_threshold: int, write_timeout: int) -> 'Process':
        """Configure the traffic server process.

        :param tr: The TestRun with which to associate the traffic server.
        :param dns_port: The MicroDNS server port that traffic server should connect to.
        :param server_port: The server port that traffic server should connect to.
        :param write_threshold: The value to set for proxy.config.http2.write_size_threshold.
        :param write_timeout: The value to set for proxy.config.http2.write_time_threshold.
        :return: The traffic server process.
        """
        self._ts = tr.MakeATSProcess("ts", enable_tls=True, enable_cache=False)

        self._ts.addDefaultSSLFiles()
        self._ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

        self._ts.Disk.remap_config.AddLine(f"map / https://example.com:{server_port}/")

        self._ts.Disk.records_config.update(
            {
                "proxy.config.ssl.server.cert.path": self._ts.Variables.SSLDir,
                "proxy.config.ssl.server.private_key.path": self._ts.Variables.SSLDir,
                'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
                'proxy.config.http.server_session_sharing.pool': 'thread',
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.dns.nameservers': f"127.0.0.1:{dns_port}",
                'proxy.config.dns.resolv_conf': "NULL",
                'proxy.config.http2.write_size_threshold': write_threshold,
                'proxy.config.http2.write_time_threshold': write_timeout,

                # Only enable debug logging during manual exectution. All the
                # DATA frames get multiple logs and it makes the traffic.out too
                # unwieldy.
                "proxy.config.diags.debug.enabled": 0,
                "proxy.config.diags.debug.tags": "http",
            })
        return self._ts

    def _configure_h2_client(self, tr: 'TestRun', proxy_port: int, write_timeout: int) -> None:
        """Start the HTTP/2 client.

        :param tr: The TestRun with which to associate the client.
        :param proxy_port: The proxy_port to which to connect.
        """
        tr.Setup.Copy('trickle_client.py')
        ca = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
        key = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")

        self._server.Setup.Copy(ca)
        self._server.Setup.Copy(key)
        # The cert is for example.com, so we must use that domain.
        hostname = 'example.com'
        command = (f'{sys.executable} {tr.RunDirectory}/trickle_client.py '
                   f'{hostname} {proxy_port} server.pem {write_timeout}')
        p = tr.Processes.Default
        p.Command = command
        p.ReturnCode = 0


test = TestGrpc("Test proxy.config.http2.write_size_threshold", 0.5, 10)
