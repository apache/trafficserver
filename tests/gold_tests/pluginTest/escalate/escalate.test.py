'''
Verify escalate plugin behavior.
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
    Test the escalate plugin default behavior (GET requests only).
    """

    _replay_original_file: str = 'escalate_original.replay.yaml'
    _replay_failover_file: str = 'escalate_failover.replay.yaml'
    _process_counter: int = 0

    def __init__(self, disable_redirect_header: bool = False) -> None:
        '''Configure the test run.
        :param disable_redirect_header: Whether to use --no-redirect-header.
        '''
        tr = Test.AddTestRun(f'Test escalate plugin. disable_redirect_header={disable_redirect_header}')
        self._setup_dns(tr)
        self._setup_servers(tr, disable_redirect_header)
        self._setup_ts(tr, disable_redirect_header)
        self._setup_client(tr)
        EscalateTest._process_counter += 1

    def _setup_dns(self, tr: 'TestRun') -> None:
        '''Set up the DNS server.

        :param tr: The test run to add the DNS server to.
        '''
        process_name = f"dns_{EscalateTest._process_counter}"
        self._dns = tr.MakeDNServer(process_name, default='127.0.0.1')

    def _setup_servers(self, tr: 'TestRun', disable_redirect_header: bool) -> None:
        '''Set up the origin and failover servers.

        :param tr: The test run to add the servers to.
        :param disable_redirect_header: Whether ATS was configured with --no-redirect-header.
        '''
        tr.Setup.Copy(self._replay_original_file)
        tr.Setup.Copy(self._replay_failover_file)
        process_name = f"server_origin_{EscalateTest._process_counter}"
        self._server_origin = tr.AddVerifierServerProcess(process_name, self._replay_original_file)
        process_name = f"server_failover_{EscalateTest._process_counter}"
        self._server_failover = tr.AddVerifierServerProcess(process_name, self._replay_failover_file)

        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET', "Verify the origin server received the GET request.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET_chunked', "Verify the origin server GET request for chunked content.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET_failed', "Verify the origin server received the GET request that it returns a 502 with.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: HEAD_fail_not_escalated', "Verify the origin server received the HEAD request that should not be escalated.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: POST_fail_not_escalated', "Verify the origin server received the POST request that should not be escalated.")
        self._server_origin.Streams.All += Testers.ExcludesExpression(
            'uuid: GET_down_origin', "Verify the origin server did not receive the down origin request.")
        self._server_origin.Streams.All += Testers.ExcludesExpression(
            'x-escalate-redirect', "Verify the origin server should never receive the x-escalate-redirect header.")

        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: GET_failed', "Verify the failover server received the failed GET request.")
        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: GET_down_origin', "Verify the failover server received the GET request for the down origin.")

        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'x-request: first', "Verify the failover server did not receive the GET request.")
        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'uuid: GET_chunked', "Verify the failover server did not receive the GET request for chunked content.")
        # By default, non-GET methods should NOT be escalated to failover
        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'uuid: HEAD_fail_not_escalated',
            "Verify the failover server did not receive the HEAD request that should not be escalated.")
        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'uuid: POST_fail_not_escalated',
            "Verify the failover server did not receive the POST request that should not be escalated.")

        if disable_redirect_header:
            self._server_failover.Streams.All += Testers.ExcludesExpression(
                'x-escalate-redirect', "Verify the failover server did not receive the x-escalate-redirect header.")
        else:
            self._server_failover.Streams.All += Testers.ContainsExpression(
                'x-escalate-redirect: 1', "Verify the failover server received the x-escalate-redirect header.")

    def _setup_ts(self, tr: 'Process', disable_redirect_header: bool) -> None:
        '''Set up Traffic Server.

        :param tr: The test run to add Traffic Server to.
        :param disable_redirect_header: Whether ATS should be configured with --no-redirect-header.
        '''
        process_name = f"ts_{EscalateTest._process_counter}"
        self._ts = tr.MakeATSProcess(process_name, enable_cache=False)
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
        params = ''
        if disable_redirect_header:
            params = '@pparam=--no-redirect-header'
        self._ts.Disk.remap_config.AddLines(
            [
                f'map http://origin.server.com http://backend.origin.server.com:{self._server_origin.Variables.http_port} '
                f'@plugin=escalate.so @pparam=500,502:failover.server.com:{self._server_failover.Variables.http_port} {params}',

                # Now create remap entries for the multiplexed hosts: one that
                # verifies HTTP, and another that verifies HTTPS.
                f'map http://down_origin.server.com http://backend.down_origin.server.com:{dead_port} '
                f'@plugin=escalate.so @pparam=500,502:failover.server.com:{self._server_failover.Variables.http_port} {params}',
            ])

    def _setup_client(self, tr: 'Process') -> None:
        '''Set up the client.

        :param tr: The test run to add the client to.
        '''
        process_name = f"client_{EscalateTest._process_counter}"
        client = tr.AddVerifierClientProcess(process_name, self._replay_original_file, http_ports=[self._ts.Variables.port])
        client.StartBefore(self._dns)
        client.StartBefore(self._server_origin)
        client.StartBefore(self._server_failover)
        client.StartBefore(self._ts)

        client.Streams.All += Testers.ExcludesExpression(r'\[ERROR\]', 'Verify there were no errors in the replay.')
        client.Streams.All += Testers.ExcludesExpression('400 Bad', 'Verify none of the 400 responses make it to the client.')
        client.Streams.All += Testers.ExcludesExpression('500 Internal', 'Verify none of the 500 responses make it to the client.')
        # GET requests should be escalated and return 200
        client.Streams.All += Testers.ContainsExpression('x-response: first', 'Verify that the first response was received.')
        client.Streams.All += Testers.ContainsExpression('x-response: second', 'Verify that the second response was received.')
        client.Streams.All += Testers.ContainsExpression('x-response: third', 'Verify that the third response was received.')
        client.Streams.All += Testers.ContainsExpression('x-response: fourth', 'Verify that the fourth response was received.')
        # Non-GET requests should NOT be escalated and return 502 (default behavior)
        client.Streams.All += Testers.ContainsExpression(
            'x-response: head_fail_not_escalated', 'Verify that the HEAD response was received (502).')
        client.Streams.All += Testers.ContainsExpression(
            'x-response: post_fail_not_escalated', 'Verify that the POST response was received (502).')
        client.Streams.All += Testers.ContainsExpression(
            '502 Bad Gateway', 'Verify that non-GET requests return 502 (not escalated by default).')


class EscalateNonGetMethodsTest:
    """
    Test the escalate plugin with --escalate-non-get-methods option to verify non-GET requests are also escalated.
    """

    _replay_get_method_file: str = 'escalate_non_get_methods.replay.yaml'
    _replay_failover_file: str = 'escalate_failover.replay.yaml'

    def __init__(self):
        '''Configure the test run for escalating non-GET methods testing.'''
        tr = Test.AddTestRun('Test escalate plugin with --escalate-non-get-methods option.')
        self._setup_dns(tr)
        self._setup_servers(tr)
        self._setup_ts(tr)
        self._setup_client(tr)

    def _setup_dns(self, tr: 'TestRun') -> None:
        '''Set up the DNS server.'''
        self._dns = tr.MakeDNServer("dns_non_get_methods", default='127.0.0.1')

    def _setup_servers(self, tr: 'TestRun') -> None:
        '''Set up the origin and failover servers for non-GET methods testing.'''
        tr.Setup.Copy(self._replay_get_method_file)
        tr.Setup.Copy(self._replay_failover_file)
        self._server_origin = tr.AddVerifierServerProcess("server_origin_non_get_methods", self._replay_get_method_file)
        self._server_failover = tr.AddVerifierServerProcess("server_failover_non_get_methods", self._replay_failover_file)

        # Verify the origin server received all requests
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET', "Verify the origin server received the first GET request.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET_chunked', "Verify the origin server received the chunked GET request.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: GET_failed', "Verify the origin server received the failed GET request.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: POST_success', "Verify the origin server received the successful POST request.")
        self._server_origin.Streams.All += Testers.ContainsExpression(
            'uuid: HEAD_fail_escalated', "Verify the origin server received the HEAD request that will be escalated.")

        # The down origin request should NOT be received by this server
        self._server_origin.Streams.All += Testers.ExcludesExpression(
            'uuid: GET_down_origin', "Verify the origin server did not receive the down origin request.")

        # Verify failover server receives escalated requests including non-GET methods
        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: GET_failed', "Verify the failover server received the failed GET request.")
        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: GET_down_origin', "Verify the failover server received the down origin GET request.")
        # With --escalate-non-get-methods, the HEAD request should now be escalated
        self._server_failover.Streams.All += Testers.ContainsExpression(
            'uuid: HEAD_fail_escalated', "Verify the failover server received the HEAD that is now escalated.")
        # The successful POST should also not reach failover (since it succeeds on origin)
        self._server_failover.Streams.All += Testers.ExcludesExpression(
            'uuid: POST_success', "Verify the failover server did not receive the successful POST request.")

    def _setup_ts(self, tr: 'TestRun') -> None:
        '''Set up Traffic Server with --escalate-non-get-methods option.'''
        self._ts = tr.MakeATSProcess("ts_non_get_methods", enable_cache=False)

        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|escalate',
                'proxy.config.dns.nameservers': f'127.0.0.1:{self._dns.Variables.Port}',
                'proxy.config.dns.resolv_conf': 'NULL',
                'proxy.config.http.redirect.actions': 'self:follow',
                'proxy.config.http.number_of_redirections': 4,
            })

        # Set up a dead port for the down origin scenario
        dead_port = get_port(self._ts, "dead_port")

        # Configure escalate plugin with --escalate-non-get-methods option
        self._ts.Disk.remap_config.AddLines(
            [
                f'map http://origin.server.com http://backend.origin.server.com:{self._server_origin.Variables.http_port} '
                f'@plugin=escalate.so @pparam=500,502:failover.server.com:{self._server_failover.Variables.http_port} @pparam=--escalate-non-get-methods',
                f'map http://down_origin.server.com http://backend.down_origin.server.com:{dead_port} '
                f'@plugin=escalate.so @pparam=500,502:failover.server.com:{self._server_failover.Variables.http_port} @pparam=--escalate-non-get-methods',
            ])

    def _setup_client(self, tr: 'TestRun') -> None:
        '''Set up the client for non-GET methods testing.'''
        client = tr.AddVerifierClientProcess(
            "client_non_get_methods", self._replay_get_method_file, http_ports=[self._ts.Variables.port])

        client.StartBefore(self._dns)
        client.StartBefore(self._server_origin)
        client.StartBefore(self._server_failover)
        client.StartBefore(self._ts)

        # Verify that successful responses are returned for successful requests and escalated failures
        client.Streams.All += Testers.ContainsExpression('x-response: first', 'Verify first GET response received.')
        client.Streams.All += Testers.ContainsExpression('x-response: second', 'Verify second GET response received.')
        client.Streams.All += Testers.ContainsExpression('x-response: third', 'Verify third GET response received (escalated).')
        client.Streams.All += Testers.ContainsExpression('x-response: fourth', 'Verify fourth GET response received (escalated).')
        client.Streams.All += Testers.ContainsExpression('x-response: post_success', 'Verify successful POST response received.')
        client.Streams.All += Testers.ContainsExpression(
            'x-response: head_fail_escalated', 'Verify escalated HEAD response received.')

        # With --escalate-non-get-methods, POST and HEAD failures should now be escalated and return 200
        client.Streams.All += Testers.ExcludesExpression(
            '502 Bad Gateway', 'Verify failed POST and HEAD requests are now escalated')

        # The test should complete without errors
        client.Streams.All += Testers.ExcludesExpression(r'\[ERROR\]', 'Verify there were no errors in the replay.')


EscalateTest(disable_redirect_header=False)
EscalateTest(disable_redirect_header=True)
EscalateNonGetMethodsTest()
