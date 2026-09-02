'''
Verify that a chunk extension whose quoted-string value contains CR or LF is
rejected. Per RFC 9110 Section 5.6.4 a quoted-string cannot contain a bare CR or
LF, so such a chunk size line is malformed. ATS must reject the request rather
than interpreting the embedded octets as a second, smuggled request: the client
gets a single error response and the embedded GET never reaches the origin.
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

Test.Summary = '''
A chunk extension quoted-string value containing CR/LF is rejected with 400.
'''


class ChunkExtensionQuotedStringTest:

    def __init__(self):
        self._setup_origin()
        self._setup_ts()

    def _setup_origin(self):
        # A proxy-verifier origin parses chunked requests correctly and serves the
        # legitimate POST with 200 (keep-alive). If the proxy forwards the embedded
        # GET /second, the origin answers it with SECOND-ENDPOINT, so a smuggled
        # request shows up as a second response on the client.
        self._server = Test.MakeVerifierServerProcess("verifier-server", "replays/chunk_extension_quoted_string.replay.yaml")

    def _setup_ts(self):
        self._ts = Test.MakeATSProcess("ts", enable_cache=False)
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 0,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.strict_chunk_parsing': 1,
            })
        self._ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{self._server.Variables.http_port}')

    def _check(self, tr):
        tr.Processes.Default.ReturnCode = 0
        # The malformed chunk size line is rejected, so the client gets exactly one
        # error response and the embedded GET /second is never forwarded. A parser
        # that ignores the quoted-string would forward that GET as a second request
        # and the origin's SECOND-ENDPOINT body would reach the client (two
        # responses). Keying on the smuggled body is deterministic; the exact error
        # status is not (ATS may relay the origin's response to the forwarded POST
        # headers, or generate its own 400).
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("responses=1", "the client must get exactly one response")
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            "SECOND-ENDPOINT", "the embedded GET must not be smuggled to the origin")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def run(self):
        # Case 1: the whole request arrives in one read.
        tr = Test.AddTestRun("Quoted extension value containing CRLF")
        tr.Setup.Copy("chunk_extension_client.py")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = f'python3 chunk_extension_client.py 127.0.0.1 {self._ts.Variables.port}'
        self._check(tr)

        # Case 2: the same request split across two writes at a read boundary
        # inside the extension. The proxy must resume parsing and still reject it.
        tr = Test.AddTestRun("Quoted extension split across reads")
        tr.Setup.Copy("chunk_extension_client.py")
        tr.Processes.Default.Command = f'python3 chunk_extension_client.py 127.0.0.1 {self._ts.Variables.port} --split'
        self._check(tr)


ChunkExtensionQuotedStringTest().run()
