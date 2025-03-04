'''
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

import sys


class ExpectTest:

    _expect_client: str = 'expect_client.py'
    _http_utils: str = 'http_utils.py'
    _replay_file: str = 'replay/expect-continue.replay.yaml'

    def __init__(self):
        tr = Test.AddTestRun('Verify Expect: 100-Continue handling.')
        self._setup_dns(tr)
        self._setup_origin(tr)
        self._setup_trafficserver(tr)
        self._setup_client(tr)

    def _setup_dns(self, tr: 'TestRun') -> None:
        '''Set up the DNS server.

        :param tr: The TestRun to which to add the DNS server.
        '''
        dns = tr.MakeDNServer('dns', default='127.0.0.1')
        self._dns = dns

    def _setup_origin(self, tr: 'TestRun') -> None:
        '''Set up the origin server.

        :param tr: The TestRun to which to add the origin server.
        '''
        server = tr.AddVerifierServerProcess("server", replay_path=self._replay_file)
        self._server = server

    def _setup_trafficserver(self, tr: 'TestRun') -> None:
        '''Set up the traffic server.

        :param tr: The TestRun to which to add the traffic server.
        '''
        ts = tr.MakeATSProcess("ts", enable_cache=False)
        self._ts = ts
        ts.Disk.remap_config.AddLine(f'map / http://backend.example.com:{self._server.Variables.http_port}')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.dns.nameservers': f"127.0.0.1:{self._dns.Variables.Port}",
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.http.send_100_continue_response': 1,
            })

    def _setup_client(self, tr: 'TestRun') -> None:
        '''Set up the client.

        :param tr: The TestRun to which to add the client.
        '''
        tr.Setup.CopyAs(self._expect_client)
        tr.Setup.CopyAs(self._http_utils)
        tr.Processes.Default.Command = \
            f'{sys.executable} {self._expect_client} 127.0.0.1 {self._ts.Variables.port} -s example.com'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self._dns)
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'HTTP/1.1 100', 'Verify the 100 Continue response was received.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'HTTP/1.1 200', 'Verify the 200 OK response was received.')


Test.Summary = 'Verify Expect: 100-Continue handling.'
ExpectTest()
