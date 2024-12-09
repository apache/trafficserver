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

import os
from ports import get_port

Test.Summary = '''
Test the escalate plugin.
'''

Test.SkipUnless(Condition.PluginExists('escalate.so'))


class EscalateTest:
    """
    Test the escalate plugin.
    """

    _replay_original_file: str = 'escalate_original.replay.yaml'
    _replay_failover_file: str = 'escalate_failover.replay.yaml'

    def __init__(self):
        '''Configure the test run.'''
        tr = Test.AddTestRun('Test escalate plugin.')
        self._setup_dns(tr)
        self._setup_servers(tr)
        self._setup_ts(tr)
        self._setup_client(tr)

    def _setup_dns(self, tr: 'Process') -> None:
        '''Set up the DNS server.

        :param tr: The test run to add the DNS server to.
        '''
        self._dns = tr.MakeDNServer(f"dns", default='127.0.0.1')

    def _setup_servers(self, tr: 'Process') -> None:
        '''Set up the origin and failover servers.

        :param tr: The test run to add the servers to.
        '''
        tr.Setup.Copy(self._replay_original_file)
        tr.Setup.Copy(self._replay_failover_file)
        self._server_origin = tr.AddVerifierServerProcess(f"server_origin", self._replay_original_file)
        self._server_failover = tr.AddVerifierServerProcess(f"server_failover", self._replay_failover_file)

        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET', "Verify the origin server received the GET request.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET_chunked', "Verify the origin server GET request for chunked content.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET_failed', "Verify the origin server received the GET request that it returns a 502 with.")
        self._server_origin.Streams.All += Testers.ExcludesExpression(
            'uuid: GET_down_origin', "Verify the origin server did not receive the down origin request.")

        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: GET_failed', "Verify the failover server received the failed GET request.")
        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: GET_down_origin', "Verify the failover server received the GET request for the down origin.")

        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'x-request: first', "Verify the failover server did not receive the GET request.")
        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'uuid: GET_chunked', "Verify the failover server did not receive the GET request for chunked content.")

    def _setup_ts(self, tr: 'Process') -> None:
        '''Set up Traffic Server.

        :param tr: The test run to add Traffic Server to.
        '''
        self._ts = tr.MakeATSProcess(f"ts", enable_cache=False)
        # Select a port that is guaranteed to not be used at the moment.
        dead_port = get_port(self._ts, "dead_port")
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|escalate',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.http.redirect.actions': 'self:follow',
                'proxy.config.http.number_of_redirections': 4,
            })
        self._ts.Disk.remap_config.AddLines(
            [
                f'map http://origin.server.com http://backend.origin.server.com:{self._server_origin.Variables.http_port} '
                f'@plugin=escalate.so @pparam=500,502:failover.server.com:{self._server_failover.Variables.http_port}',

                # Now create remap entries for the multiplexed hosts: one that
                # verifies HTTP, and another that verifies HTTPS.
                f'map http://down_origin.server.com http://backend.down_origin.server.com:{dead_port} '
                f'@plugin=escalate.so @pparam=500,502:failover.server.com:{self._server_failover.Variables.http_port} ',
            ])

    def _setup_client(self, tr: 'Process') -> None:
        '''Set up the client.

        :param tr: The test run to add the client to.
        '''
        client = tr.AddVerifierClientProcess(f"client", self._replay_original_file, http_ports=[self._ts.Variables.port])
        client.StartBefore(self._dns)
        client.StartBefore(self._server_origin)
        client.StartBefore(self._server_failover)
        client.StartBefore(self._ts)

        client.Streams.All += Testers.ExcludesExpression(r'\[ERROR\]', 'Verify there were no errors in the replay.')
        client.Streams.All += Testers.ExcludesExpression('400 Bad', 'Verify none of the 400 responses make it to the client.')
        client.Streams.All += Testers.ExcludesExpression('502 Bad', 'Verify none of the 502 responses make it to the client.')
        client.Streams.All += Testers.ExcludesExpression('500 Internal', 'Verify none of the 500 responses make it to the client.')
        client.Streams.All += Testers.ContainsExpression('x-response: first', 'Verify that the first response was received.')
        client.Streams.All += Testers.ContainsExpression('x-response: second', 'Verify that the second response was received.')
        client.Streams.All += Testers.ContainsExpression('x-response: third', 'Verify that the third response was received.')
        client.Streams.All += Testers.ContainsExpression('x-response: fourth', 'Verify that the fourth response was received.')


EscalateTest()
