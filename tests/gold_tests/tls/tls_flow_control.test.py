'''
Exercise HTTP tunnel flow control (proxy.config.http.flow_control) on a TLS
client connection: a slow reader makes ATS throttle the origin and unthrottle as
the buffered data drains. The full response body must still be delivered.
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


class TestTlsFlowControl:
    '''Verify a slow TLS reader under flow control still receives the whole body.'''

    # Comfortably larger than the water marks so the tunnel throttles and has to
    # unthrottle many times over the transfer.
    _body_len: int = 8 * 1024 * 1024
    _high_water: int = 64 * 1024
    _low_water: int = 32 * 1024

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._server = self._configure_server()
        self._ts = self._configure_trafficserver()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server with a large response body.

        :return: The origin server Process.
        '''
        server = Test.MakeOriginServer(f'server-{TestTlsFlowControl._server_counter}')
        TestTlsFlowControl._server_counter += 1

        request_header = {"headers": "GET /obj HTTP/1.1\r\nHost: ex.test\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {
            "headers":
                "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n"
                f"Content-Length: {TestTlsFlowControl._body_len}\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": "x" * TestTlsFlowControl._body_len
        }
        server.addResponse("sessionlog.json", request_header, response_header)
        return server

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server with HTTP flow control enabled over TLS.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestTlsFlowControl._ts_counter}', enable_tls=True, enable_cache=False)
        TestTlsFlowControl._ts_counter += 1

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                # Small thresholds on a network sink: the tunnel reenable fires before
                # the socket write, so the unthrottle depends on the buffer draining.
                'proxy.config.http.flow_control.enabled': 1,
                'proxy.config.http.flow_control.high_water': TestTlsFlowControl._high_water,
                'proxy.config.http.flow_control.low_water': TestTlsFlowControl._low_water,
            })

        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "received signal|failed assertion", "ATS must not crash under TLS flow control")
        ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
            "AddressSanitizer|use-after-free|runtime error:", "no memory-safety error under TLS flow control")
        return ts

    def run(self) -> None:
        '''Configure and run the TestRun.'''
        tr = Test.AddTestRun("slow TLS reader under flow control must receive the whole body")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_flow_control_client.py")} '
            f'-p {self._ts.Variables.ssl_port} --host ex.test --path /obj '
            f'--expect-bytes {TestTlsFlowControl._body_len}')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            "RESULT=PASS", "the full body must be delivered under flow control")
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            f"BODY_BYTES={TestTlsFlowControl._body_len}", "every body byte must arrive (no flow-control stall)")
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


TestTlsFlowControl().run()
