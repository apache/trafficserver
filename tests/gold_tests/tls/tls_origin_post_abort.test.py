'''
When a TLS origin resets the connection while ATS is still sending it a POST
request body, ATS must fail the transaction promptly -- the transport error must
reach the request-body tunnel right away as VC_EVENT_ERROR, not sit until the
outbound inactivity timeout rescues it.
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
import sys

Test.Summary = __doc__


class TestOriginPostAbort:
    '''Verify a TLS origin RST mid-POST-body fails the transaction promptly.'''

    _ts_counter: int = 0
    _origin_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        Test.GetTcpPort("origin_port")
        self._ts = self._configure_trafficserver()
        self._origin = self._configure_origin()

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server with a TLS origin and a generous rescue timeout.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestOriginPostAbort._ts_counter}', enable_cache=False)
        TestOriginPostAbort._ts_counter += 1

        ts.Disk.records_config.update(
            {
                'proxy.config.url_remap.remap_required': 1,
                'proxy.config.http.connect_attempts_max_retries': 0,
                'proxy.config.http.connect_attempts_timeout': 15,
                # The rescue timeout: a prompt failure must beat this by a wide margin.
                'proxy.config.http.transaction_no_activity_timeout_out': 15,
                'proxy.config.http.transaction_no_activity_timeout_in': 30,
                # Keep the kernel send buffer small so the request body cannot be
                # absorbed by the kernel: ATS must still be mid-send at the RST.
                'proxy.config.net.sock_send_buffer_size_out': 65536,
                'proxy.config.ssl.client.verify.server.policy': 'DISABLED',
                'proxy.config.diags.debug.enabled': 0,
                'proxy.config.diags.debug.tags': 'http|ssl|ssl_io',
            })
        ts.Disk.remap_config.AddLine(f'map /post https://127.0.0.1:{Test.Variables.origin_port}')

        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            'received signal|failed assertion', 'ATS must not crash handling the mid-body origin abort')
        return ts

    def _configure_origin(self) -> 'Process':
        '''Configure a raw-socket TLS origin that resets mid-request-body.

        The scenario needs a TLS origin that accepts the request body and then RSTs
        the connection mid-body (SO_LINGER 0). Proxy Verifier can only close cleanly
        (close_notify), so it cannot express a mid-body reset; hence the small
        raw-socket origin helper rather than an ATSReplayTest.

        :return: The origin server Process.
        '''
        origin = Test.Processes.Process(
            f'origin-{TestOriginPostAbort._origin_counter}',
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_post_abort_origin.py")} '
            f'-p {Test.Variables.origin_port} '
            f'-c {os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")} -d 1.0')
        TestOriginPostAbort._origin_counter += 1

        # These markers prove the reset path was actually exercised: the origin
        # completed the TLS handshake, received the forwarded request headers, and
        # then reset the connection mid-body. Without them the client could pass on
        # any prompt response -- e.g. a broken remap or a never-started origin that
        # yields a fast 5xx without the body ever reaching a resetting origin.
        origin.Streams.stdout = Testers.ContainsExpression(
            'request headers received', 'the origin must receive the forwarded request headers')
        origin.Streams.stdout += Testers.ContainsExpression(
            'connection reset sent', 'the origin must reset the connection mid-body')
        return origin

    def run(self) -> None:
        '''Configure and run the TestRun.'''
        tr = Test.AddTestRun("POST to a TLS origin that resets mid-body must fail promptly")
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.StartBefore(self._origin, ready=When.PortOpen(Test.Variables.origin_port))
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_post_abort_client.py")} '
            f'-p {self._ts.Variables.port} -t 8')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'PASS: transaction failed promptly', 'the POST must fail promptly on the origin RST')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'status-code: 5', 'ATS must surface the mid-body origin reset as a 5xx')
        tr.StillRunningAfter = self._ts


TestOriginPostAbort().run()
