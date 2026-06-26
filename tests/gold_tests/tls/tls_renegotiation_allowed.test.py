'''
With client renegotiation allowed, a TLSv1.2 client that asks to renegotiate must
get a prompt answer from ATS (the renegotiation either completes or is refused
with an alert) rather than hanging. ATS must flush the SSL protocol output the
renegotiation generates; otherwise the answer is never sent and the client
stalls.
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

from ports import get_port

Test.Summary = __doc__

# Renegotiation only exists in TLS 1.2 and earlier.
Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"), Condition.HasLegacyTLSSupport())


class TestRenegotiationAllowed:
    '''Verify an allowed client renegotiation is answered promptly, not stranded.'''

    _server_counter: int = 0
    _ts_counter: int = 0

    def __init__(self) -> None:
        '''Declare the test Processes.'''
        self._server = self._configure_server()
        self._ts = self._configure_trafficserver()

    def _configure_server(self) -> 'Process':
        '''Configure a plain HTTP/1.0 origin that closes each connection.

        A python http.server origin answers each request on its own connection and
        closes it, so ATS's client-facing TLS write face is fully drained between
        requests. When the renegotiation ClientHello then arrives there is no
        in-flight transport write to coincidentally carry the renegotiation answer
        out -- exactly the condition under which a proxy that fails to flush its own
        SSL protocol output would strand the answer and hang the client. (The
        Proxy-Verifier microserver keeps the response connection in a state that
        masks the stall, so it is unsuitable for this regression.)

        :return: The origin server Process.
        '''
        server = Test.Processes.Process(f'server-{TestRenegotiationAllowed._server_counter}')
        TestRenegotiationAllowed._server_counter += 1

        origin_dir = os.path.join(Test.RunDirectory, "origin")
        os.makedirs(origin_dir, exist_ok=True)
        with open(os.path.join(origin_dir, "index.html"), "w") as f:
            f.write("ok\n")

        origin_port = get_port(server, "http_port")
        server.Command = f'{sys.executable} -m http.server {origin_port} --bind 127.0.0.1 --directory {origin_dir}'
        server.Ready = When.PortOpenv4(origin_port)
        # The server is a long-running process the harness terminates at the end of
        # the test, so its exit status is whatever the signal leaves behind.
        server.ReturnCode = Any(None, 0, -2, -15)
        return server

    def _configure_trafficserver(self) -> 'Process':
        '''Configure Traffic Server to honor client renegotiation.

        :return: The Traffic Server Process.
        '''
        ts = Test.MakeATSProcess(f'ts-{TestRenegotiationAllowed._ts_counter}', enable_tls=True)
        TestRenegotiationAllowed._ts_counter += 1

        ts.addSSLfile("ssl/server.pem")
        ts.addSSLfile("ssl/server.key")
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                # The operator opts in: honor a client-initiated renegotiation.
                'proxy.config.ssl.allow_client_renegotiation': 1,
                # Renegotiation requires a sub-TLS1.3 protocol.
                'proxy.config.ssl.TLSv1_3.enabled': 0,
                'proxy.config.ssl.TLSv1_2': 1,
            })
        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')

        # The renegotiation must not abort the process.
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "received signal|failed assertion", "ATS must service the renegotiation without crashing")
        return ts

    def run(self) -> None:
        '''Configure and run the TestRuns.'''
        # Complete a TLSv1.2 handshake and one request, let the response drain, ask
        # to renegotiate, then drive a second request. ATS must answer the
        # renegotiation promptly; the helper reports RENEGOTIATION-STALLED (and
        # exits non-zero) if the client hangs waiting for the answer.
        tr = Test.AddTestRun("client renegotiation is answered promptly")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = (
            f'{sys.executable} {os.path.join(Test.TestDirectory, "tls_renegotiation_allowed_client.py")} '
            f'-p {self._ts.Variables.ssl_port} -s example.com')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            "RENEGOTIATION-COMPLETED|RENEGOTIATION-REFUSED-PROMPTLY", "the renegotiation must be answered, not stranded")
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            "RENEGOTIATION-STALLED", "the client must not hang waiting for the renegotiation answer")
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

        # ATS survived and keeps serving, so a normal request still succeeds.
        tr = Test.AddTestRun("server still serves requests after the renegotiation")
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


TestRenegotiationAllowed().run()
