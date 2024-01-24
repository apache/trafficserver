"""Test basic gRPC traffic."""

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
    """Test basic gRPC traffic."""

    def __init__(self, description: str):
        """Configure a TestRun for gRPC traffic.

        :param description: The description for the test runs.
        """
        self._description = description

    def _configure_dns(self, tr: 'TestRun') -> 'Process':
        """Configure a locally running MicroDNS server.

        :param tr: The TestRun with which to associate the MicroDNS server.
        :return: The MicroDNS server process.
        """
        self._dns = tr.MakeDNServer("dns", default=['127.0.0.1'])
        return self._dns

    def _configure_traffic_server(self, tr: 'TestRun', dns_port: int, server_port: int) -> 'Process':
        """Configure the traffic server process.

        :param tr: The TestRun with which to associate the traffic server.
        :param dns_port: The MicroDNS server port that traffic server should connect to.
        :param server_port: The gRPC server port that traffic server should connect to.
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
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
            })
        return self._ts

    def _configure_grpc_server(self, tr: 'TestRun') -> 'Process':
        """Start the gRPC server.

        :param tr: The TestRun with which to associate the gRPC server.
        :return: The gRPC server process.
        """
        tr.Setup.Copy('grpc_server.py')
        self._server = tr.Processes.Process('server')

        server_pem = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
        server_key = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")
        self._server.Setup.Copy(server_pem)
        self._server.Setup.Copy(server_key)

        port = get_port(self._server, 'port')
        command = (f'{sys.executable} {tr.RunDirectory}/grpc_server.py {port} '
                   'server.pem server.key')
        self._server.Command = command
        self._server.ReturnCode = 0
        return self._server

    def _configure_grpc_client(self, tr: 'TestRun', proxy_port: int) -> None:
        """Start the gRPC client.

        :param tr: The TestRun with which to associate the gRPC client.
        :param proxy_port: The proxy_port to which to connect.
        """
        tr.Setup.Copy('grpc_client.py')
        ts_cert = os.path.join(self._ts.Variables.SSLDir, 'server.pem')
        # The cert is for example.com, so we must use that domain.
        hostname = 'example.com'
        command = (f'{sys.executable} {tr.RunDirectory}/grpc_client.py '
                   f'{hostname} {proxy_port} {ts_cert}')
        tr.Processes.Default.Command = command
        tr.Processes.Default.ReturnCode = 0

    def _compile_protobuf_files(self) -> None:
        """Compile the protobuf files."""
        tr = Test.AddTestRun(f'{self._description}: compile the protobuf files.')
        tr.Setup.Copy('simple.proto')
        command = (
            f'{sys.executable} -m grpc_tools.protoc -I{tr.RunDirectory} '
            f'--python_out={tr.RunDirectory} --grpc_python_out={tr.RunDirectory} simple.proto')
        tr.Processes.Default.Command = command
        pb2_file = os.path.join(tr.RunDirectory, 'simple_pb2.py')
        tr.Disk.File(pb2_file, id='pb2', exists=True)

        pb2_grpc_file = os.path.join(tr.RunDirectory, 'simple_pb2_grpc.py')
        tr.Disk.File(pb2_grpc_file, id='pb2_grpc', exists=True)

    def _run_test_traffic(self) -> None:
        """Configure the TestRun for the client and servers."""
        tr = Test.AddTestRun(f'{self._description}: run the gRPC traffic.')

        dns = self._configure_dns(tr)
        server = self._configure_grpc_server(tr)
        ts = self._configure_traffic_server(tr, dns.Variables.Port, server.Variables.port)

        tr.Processes.Default.StartBefore(dns)
        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)

        self._configure_grpc_client(tr, ts.Variables.ssl_port)

    def run(self) -> None:
        """Configure the various test runs for the gRPC test."""
        self._compile_protobuf_files()
        self._run_test_traffic()


test = TestGrpc("Test basic gRPC traffic")
test.run()
