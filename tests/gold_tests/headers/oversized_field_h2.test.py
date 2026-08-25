'''
Verify that an HTTP/2 header whose name or value length exceeds the uint16_t
field-length limit (>= 65535 bytes) is treated as an HPACK connection error
(GOAWAY with COMPRESSION_ERROR, code 0x9) and is never forwarded to the origin.
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

Test.Summary = '''
Verify that an HTTP/2 header field whose name or value length exceeds the
uint16_t field-length limit (>= 65535 bytes) is rejected as an HPACK connection
error (GOAWAY COMPRESSION_ERROR, code 0x9) rather than stored with a truncated
length, and is never forwarded to the origin.
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'), Condition.HasProxyVerifierVersion('2.8.0'))


class OversizedFieldH2Test:
    '''Drive an oversized HTTP/2 header with a raw HPACK client.'''

    replayFile = "replay/oversized_field_h2.replay.yaml"
    clientScript = "oversized_field_h2_client.py"

    # Header sizes large enough to exceed the uint16_t (65535) field-length limit.
    oversizedSize = 70000

    def __init__(self):
        self.__setupOriginServer()
        self.__setupTS()
        self.__setupClient()

    def __setupOriginServer(self):
        self._server = Test.MakeVerifierServerProcess("verifier-server", self.replayFile)
        # The origin must never receive the oversized requests. If ATS forwarded
        # them (the pre-fix behavior), the verifier server would log a request
        # for these paths / serve the marker bodies.
        self._server.Streams.All += Testers.ExcludesExpression(
            'h2-oversized-value', 'Origin must not receive the oversized-value request.')
        self._server.Streams.All += Testers.ExcludesExpression(
            'h2-oversized-name', 'Origin must not receive the oversized-name request.')
        # Regression guard: the normal, under-limit request MUST reach the origin.
        self._server.Streams.All += Testers.ContainsExpression('h2-normal', 'Origin must receive the normal under-limit request.')

    def __setupTS(self):
        self._ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http2|hpack',
                'proxy.config.ssl.server.cert.path': f"{self._ts.Variables.SSLDir}",
                'proxy.config.ssl.server.private_key.path': f"{self._ts.Variables.SSLDir}",
                # max_header_list_size is raised well above the oversized field so the
                # request is not rejected at the HTTP/2 header-list-size level first.
                # header_field_max_size is set to its maximum (65535, the uint16_t
                # ceiling enforced by the records range check); a field larger than that
                # is rejected by the configured field-size limit with a COMPRESSION_ERROR.
                # The uint16_t storage limit in the MIME setters is exercised directly by
                # the HpackIndexingTable unit test: header_field_max_size can no longer be
                # configured above 65535, so an oversized field can no longer reach the
                # storage path end to end through config.
                'proxy.config.http.header_field_max_size': 65535,
                'proxy.config.http2.max_header_list_size': 8 * 1024 * 1024,
            })
        # Rejecting the oversized field is an HPACK connection error, so ATS
        # intentionally logs "ERROR: HTTP/2 connection error code=0x09 ...
        # compression error". Whitelist exactly that line; the default check
        # treats any "ERROR:" in diags.log as a failure, so assert this
        # expected line is present instead.
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r"ERROR: HTTP/2 connection error code=0x09 .* compression error",
            "ATS must log the expected HTTP/2 COMPRESSION_ERROR for the oversized field.")
        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.http_port}")
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

    def __setupClient(self):
        self._ts.Setup.CopyAs(f"clients/{self.clientScript}", Test.RunDirectory)

    def run(self):
        # Case 1: oversized header VALUE.
        tr = Test.AddTestRun("oversized H2 header value")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        port = self._ts.Variables.ssl_port
        tr.Processes.Default.Command = (
            f"{sys.executable} {self.clientScript} /h2-oversized-value 0 {self.oversizedSize} 127.0.0.1 {port} example.com")
        tr.Processes.Default.ReturnCode = 0
        # No :status (connection error, not a response) and GOAWAY COMPRESSION_ERROR (0x9).
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'status=None', 'Client must not get an HTTP response for the oversized header.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'goaway_error=9', 'ATS must send GOAWAY with COMPRESSION_ERROR (0x9).')
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

        # Case 2: oversized header NAME.
        tr = Test.AddTestRun("oversized H2 header name")
        tr.Processes.Default.Command = (
            f"{sys.executable} {self.clientScript} /h2-oversized-name {self.oversizedSize} 0 127.0.0.1 {port} example.com")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'status=None', 'Client must not get an HTTP response for the oversized header.')
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'goaway_error=9', 'ATS must send GOAWAY with COMPRESSION_ERROR (0x9).')
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

        # Case 3: NORMAL, under-limit request (regression guard against
        # over-rejection). Sanity mode "0 0" sends a plain GET; the client must
        # get a 200 and the origin must receive it.
        tr = Test.AddTestRun("normal under-limit H2 request")
        tr.Processes.Default.Command = (f"{sys.executable} {self.clientScript} /h2-normal 0 0 127.0.0.1 {port} example.com")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            'status=200', 'Client must get a 200 for the normal under-limit request.')
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


OversizedFieldH2Test().run()
