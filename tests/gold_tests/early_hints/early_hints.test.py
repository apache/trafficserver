'''
Verify correct handling of 103 Early Hints responses.
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

from enum import Enum, auto
import os
from ports import get_port
import re
import sys


class Protocol(Enum):
    HTTP = auto()
    HTTPS = auto()
    HTTP2 = auto()

    @classmethod
    def to_string(cls, protocol):
        if protocol == cls.HTTP:
            return 'HTTP'
        elif protocol == cls.HTTPS:
            return 'HTTPS'
        elif protocol == cls.HTTP2:
            return 'HTTP2'
        else:
            return None


class TestEarlyHints:
    '''Verify that ATS can properly handle a 103 response.'''

    _early_hints_server = 'early_hints_server.py'

    def __init__(self, protocol: Protocol):
        '''Create a test run for the given protocol.
        :param protocol: The protocol the client will use for the test.
        '''
        self._protocol = protocol
        self._protocol_str = Protocol.to_string(protocol)
        tr = Test.AddTestRun(f'Early hints with client protocol: {self._protocol_str}')
        self._configure_dns(tr)
        self._configure_server(tr)
        ts = self._configure_ts(tr)
        self._copy_scripts(tr, ts)
        self._configure_client(tr)

    def _configure_dns(self, tr: 'TestRun'):
        '''Configure the DNS for the test run.
        :param tr: The TestRun for the DNS process.
        '''
        dns = tr.MakeDNServer(f'dns_{self._protocol_str}', default='127.0.0.1')
        self._dns = dns
        return dns

    def _configure_server(self, tr: 'TestRun'):
        '''Configure the origin server for the test run.

        :param tr: The TestRun for the origin server.
        '''
        tr.Setup.Copy(self._early_hints_server)
        server = tr.Processes.Process(f'server_{self._protocol_str}')
        server_port = get_port(server, "http_port")
        server.Command = \
            f'{sys.executable} {self._early_hints_server} 127.0.0.1 {server_port} '
        server.Ready = When.PortOpenv4(server_port)

        self._server = server
        return server

    def _configure_ts(self, tr: 'TestRun'):
        '''Configure the traffic server for the test run.

        :param tr: The TestRun for the traffic server.
        '''
        ts = Test.MakeATSProcess(f'ts_{self._protocol_str}', enable_tls=True)
        self._ts = ts
        ts.Disk.remap_config.AddLine(f'map / http://backend.server.com:{self._server.Variables.http_port}')
        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
            })
        return ts

    def _copy_scripts(self, tr: 'TestRun', ts: 'TestATSProcess'):
        '''Copy the python server and helper files to the test run directory.
        :param tr: The TestRun for the server.
        :param ts: The TestATSProcess for the traffic server. This is needed
          for the ATS tools directory it stores in Variables.
        '''
        tr.Setup.Copy(self._early_hints_server)
        tools_dir = ts.Variables.AtsTestToolsDir
        http_utils = os.path.join(tools_dir, 'http_utils.py')
        tr.Setup.CopyAs(http_utils, Test.RunDirectory)

    def _configure_client(self, tr: 'TestRun'):
        '''Configure a client to use the given protocol to ATS.
        :param tr: The TestRun for the client.
        '''
        client = tr.Processes.Default
        if self._protocol == Protocol.HTTP:
            protocol_arg = '--http1.1'
            scheme = 'http'
            ts_port = self._ts.Variables.port
        elif self._protocol == Protocol.HTTPS:
            protocol_arg = '-k --http1.1'
            scheme = 'https'
            ts_port = self._ts.Variables.ssl_port
        elif self._protocol == Protocol.HTTP2:
            protocol_arg = '-k --http2'
            scheme = 'https'
            ts_port = self._ts.Variables.ssl_port
        client.Command = (
            f'curl -v {protocol_arg} '
            f'--resolve "server.com:{ts_port}:127.0.0.1" '
            f'-H "Host: server.com" '
            f'{scheme}://server.com:{ts_port}/{self._protocol_str}')

        client.ReturnCode = 0
        self._ts.StartBefore(self._dns)
        self._ts.StartBefore(self._server)
        client.StartBefore(self._ts)

        # Note that the server is configured to send two 103 responses.
        client.Streams.All += Testers.ContainsExpression(
            'HTTP/.* 103.*HTTP/.* 103',
            'Verify that two 103 Early Hints responses were received.',
            reflags=re.MULTILINE | re.DOTALL)
        client.Streams.All += Testers.ContainsExpression(
            'ink: </style.css>; rel=preload', 'Verify preload link header was received.')
        client.Streams.All += Testers.ContainsExpression('HTTP/.* 200', 'Verify 200 OK response was received.')
        client.Streams.All += Testers.ContainsExpression('10bytebody', 'Verify the body to the 200 OK was received.')


TestEarlyHints(Protocol.HTTP)
TestEarlyHints(Protocol.HTTPS)
TestEarlyHints(Protocol.HTTP2)
