'''
With client renegotiation disallowed (the default), a TLSv1.2 client that asks to
renegotiate must have its connection refused without taking down the server.
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

# Renegotiation only exists in TLS 1.2 and earlier.
Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"), Condition.HasLegacyTLSSupport())


class TestRenegotiationRefused:
    '''Verify a refused client-initiated renegotiation does not crash ATS.'''

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._server = self._configure_server()
        self._ts = self._configure_trafficserver()

    def _configure_server(self) -> 'Process':
        '''Configure the origin server.

        :return: The origin server Process.
        '''
        server = Test.MakeOriginServer(f'server-{TestRenegotiationRefused._server_counter}')
        TestRenegotiationRefused._server_counter += 1

        request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "ok"}
        server.addResponse("sessionlog.json", request_header, response_header)
        return server

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server to refuse client renegotiation (the default).

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestRenegotiationRefused._ts_counter}', enable_tls=True)
        TestRenegotiationRefused._ts_counter += 1

        ts.addSSLfile("ssl/server.pem")
        ts.addSSLfile("ssl/server.key")
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                # The secure default: never honor a client-initiated renegotiation.
                'proxy.config.ssl.allow_client_renegotiation': 0,
                # Renegotiation requires a sub-TLS1.3 protocol.
                'proxy.config.ssl.TLSv1_3.enabled': 0,
                'proxy.config.ssl.TLSv1_2': 1,
                # So the renegotiation-detection log line below is emitted.
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'ssl_load',
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.Port}')

        # The refused renegotiation must not abort the process.
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "received signal|failed assertion", "ATS must refuse the renegotiation without crashing")
        # ...and it must actually reach the renegotiation-detection path (otherwise
        # a no-crash pass could mean the client never managed to renegotiate at all).
        ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "trying to renegotiate from the client", "ATS must detect the client-initiated renegotiation")
        return ts

    def run(self) -> None:
        '''Configure and run the TestRuns.'''
        # Complete a TLSv1.2 handshake, then ask to renegotiate. ATS must detect
        # and refuse the renegotiation without aborting the process.
        tr = Test.AddTestRun("client renegotiation is refused without crashing ATS")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_renegotiation_client.py")} '
            f'-p {self._ts.Variables.ssl_port} -s example.com')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

        # ATS survived, so a normal request still succeeds.
        tr = Test.AddTestRun("server still serves requests after the refused renegotiation")
        tr.MakeCurlCommand(
            (
                "-v --http1.1 --tls-max 1.2 --tlsv1.2 --ciphers DEFAULT@SECLEVEL=0 -k "
                f"--resolve 'example.com:{self._ts.Variables.ssl_port}:127.0.0.1' "
                f"https://example.com:{self._ts.Variables.ssl_port}/"),
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        # curl -v writes the response status line to stderr.
        tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
            "HTTP/1.1 200 OK", "request after renegotiation should succeed")
        tr.StillRunningAfter = self._ts


TestRenegotiationRefused().run()
