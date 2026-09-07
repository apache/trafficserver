'''Verify CONNECT waits for the origin TCP handshake.'''
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

import ports

Test.Summary = 'Verify CONNECT waits for the origin TCP handshake.'


class ConnectHandshakeTest:
    '''Verify CONNECT failure is reported before establishing a tunnel.'''

    replay_file: str = 'replays/connect_handshake.replay.yaml'

    def __init__(self) -> None:
        '''Configure the test run.'''
        tr = Test.AddTestRun('CONNECT to a refused origin port')
        self._configure_unavailable_origin(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_unavailable_origin(self, tr: 'TestRun') -> 'Process':
        '''Reserve an origin port without starting a listening server.'''
        origin = tr.Processes.Process('unavailable-origin')
        ports.get_port(origin, 'Port')
        self._origin = origin
        return origin

    def _configure_traffic_server(self, tr: 'TestRun') -> 'Process':
        '''Configure Traffic Server as an explicit proxy.'''
        ts = tr.MakeATSProcess('ts', enable_cache=False)
        self._ts = ts

        origin_port = self._origin.Variables.Port
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|iocore_net',
                'proxy.config.http.connect_ports': f'{origin_port}',
            }
        )
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{origin_port}')
        ts.addPrivateConnectAllowYaml()
        return ts

    def _configure_client(self, tr: 'TestRun') -> 'Process':
        '''Configure a Proxy Verifier client that expects the refusal.'''
        client = tr.AddVerifierClientProcess('client', self.replay_file, http_ports=[self._ts.Variables.port])
        client.StartBefore(self._ts)
        return client


ConnectHandshakeTest()
