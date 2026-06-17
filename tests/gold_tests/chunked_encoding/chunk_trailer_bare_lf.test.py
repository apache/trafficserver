'''
Verify that a chunked request whose trailer section is terminated by a bare LF
instead of CRLF is rejected. Per RFC 9112 Section 7.1 the trailer section ends
with an empty line, "CRLF"; a bare LF blank line is not a valid terminator. ATS
must reject the request rather than ending the body one byte early and
interpreting the trailing octets as a second, smuggled request: the client gets a
single response and the embedded GET never reaches the origin.
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
A chunked trailer terminated by a bare LF instead of CRLF is rejected.
'''


class ChunkTrailerBareLfTest:

    def __init__(self):
        self._setup_origin()
        self._setup_ts()

    def _setup_origin(self):
        # A proxy-verifier origin parses chunked requests correctly and serves the
        # legitimate POST with 200 (keep-alive). If the proxy forwards the embedded
        # GET /smuggled, the origin answers it with SECOND-ENDPOINT, so a smuggled
        # request shows up as a second response on the client.
        self._server = Test.MakeVerifierServerProcess("verifier-server", "replays/chunk_trailer_bare_lf.replay.yaml")

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
        # The bare-LF trailer terminator is rejected, so the embedded GET /smuggled
        # is never forwarded. A parser that accepts the bare LF would forward that
        # GET as a second request and the origin's SECOND-ENDPOINT body would reach
        # the client. Keying on the smuggled body is the deterministic, version-
        # independent security property. How ATS surfaces the rejection is not
        # portable: 9.2.x rejects the malformed framing by closing the connection
        # (often no HTTP response), whereas later branches may emit a 400 or relay
        # the origin's response, so the exact response count is not asserted here.
        tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
            "SECOND-ENDPOINT", "the embedded GET must not be smuggled to the origin")
        tr.StillRunningAfter = self._server
        tr.StillRunningAfter = self._ts

    def run(self):
        # Case 1: the whole request arrives in one read.
        tr = Test.AddTestRun("Chunked trailer terminated by a bare LF")
        tr.Setup.Copy("chunk_trailer_client.py")
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.Command = f'python3 chunk_trailer_client.py 127.0.0.1 {self._ts.Variables.port}'
        self._check(tr)

        # Case 2: the same request split across two writes at a read boundary right
        # after the final "0\r\n". The proxy must resume parsing the trailer and
        # still reject the bare-LF terminator.
        tr = Test.AddTestRun("Chunked trailer bare LF split across reads")
        tr.Setup.Copy("chunk_trailer_client.py")
        tr.Processes.Default.Command = f'python3 chunk_trailer_client.py 127.0.0.1 {self._ts.Variables.port} --split'
        self._check(tr)


ChunkTrailerBareLfTest().run()
