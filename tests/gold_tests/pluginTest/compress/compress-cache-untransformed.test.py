'''
Regression test for https://github.com/apache/trafficserver/issues/12244

The crash requires two conditions in the same transaction:
1. An intermediate response (100 Continue) is forwarded to the client, which
   sets client_response_hdr_bytes to the intermediate header size via
   setup_100_continue_transfer().
2. The final response goes through a compress transform with untransformed
   cache writing (cache=true), which calls setup_server_transfer_to_transform().
   For non-chunked responses, client_response_hdr_bytes is NOT reset to 0.

perform_cache_write_action() then passes the stale client_response_hdr_bytes
as skip_bytes to the cache-write consumer, but the server-to-transform tunnel
buffer contains only body data (no headers). The assertion in
HttpTunnel::producer_run fires:

  c->skip_bytes <= c->buffer_reader->read_avail()

This test uses a custom origin that sends "100 Continue" followed by a
compressible, non-chunked 200 OK to trigger the exact crash path.

The issue itself was reported for range requests of a small, cacheable 308
response. 103 Early Hints responses are forwarded to HTTP/1.1 clients through
the same setup_100_continue_transfer() path, and a cache miss for a Range
request installs the range transform while caching the untransformed
response. The second test run reproduces that scenario: 103 Early Hints, then
a 65 byte 308, requested with "Range: bytes=0-64".
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

from ports import get_port

Test.Summary = '''
Regression test for a cache-write assertion failure when the origin sends an
interim response (100 Continue or 103 Early Hints) before a response that goes
through a transform (#12244)
'''

Test.SkipUnless(Condition.PluginExists('compress.so'))


class CompressCacheUntransformedTest:

    def __init__(self):
        self.setupTS()
        continue_tr = Test.AddTestRun("100 Continue before a compressed, cached response")
        early_hints_tr = Test.AddTestRun("103 Early Hints before a range requested, cached 308")
        self.continue_origin = self._makeOrigin(continue_tr, "continue_origin", "continue")
        self.early_hints_origin = self._makeOrigin(early_hints_tr, "early_hints_origin", "early-hints")
        self.configureTS()
        self.run100Continue(continue_tr)
        self.runEarlyHintsRange(early_hints_tr)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_cache=True)

    def _makeOrigin(self, tr, name, mode):
        tr.Setup.CopyAs("compress_100_continue_origin.py")
        tr.Setup.Copy("etc/compress-cache-false.config")

        origin = tr.Processes.Process(name)
        port = get_port(origin, 'http_port')
        origin.Command = (f'{sys.executable} compress_100_continue_origin.py'
                          f' --port {port} --mode {mode}')
        origin.Ready = When.PortOpenv4(port)
        origin.ReturnCode = 0
        tr.Processes.Default.StartBefore(origin)
        return origin

    def configureTS(self):
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|compress|http_tunnel|http_range",
                # Do NOT send 100 Continue from ATS - let the origin send it.
                # This ensures ATS processes the origin's 100 via
                # handle_100_continue_response -> setup_100_continue_transfer,
                # which sets client_response_hdr_bytes.
                "proxy.config.http.send_100_continue_response": 0,
                # Enable POST caching so that the 200 OK is cached, triggering
                # the cache write path where the stale client_response_hdr_bytes
                # causes the crash.
                "proxy.config.http.cache.post_method": 1,
                # Cache the response to a Range request on a miss so that the
                # range transform is installed alongside an untransformed
                # cache write.
                "proxy.config.http.cache.range.write": 1,
            })

        early_hints_port = self.early_hints_origin.Variables.http_port
        continue_port = self.continue_origin.Variables.http_port
        self.ts.Disk.remap_config.AddLines(
            [
                f'map /early-hints/ http://127.0.0.1:{early_hints_port}/early-hints/'
                f' @plugin=compress.so'
                f' @pparam={Test.RunDirectory}/compress-cache-false.config',
                f'map / http://127.0.0.1:{continue_port}/'
                f' @plugin=compress.so'
                f' @pparam={Test.RunDirectory}/compress-cache-false.config',
            ])

    def run100Continue(self, tr):
        # Client sends a POST with Expect: 100-continue but does not wait for
        # the 100 response before sending the body (--expect100-timeout 0).
        # The crash is triggered by ATS processing the origin's 100 Continue,
        # not by the client's behaviour during the handshake.
        client = tr.Processes.Default
        client.Command = (
            f'curl --http1.1 -s -o /dev/null'
            f' -X POST'
            f' -H "Accept-Encoding: gzip"'
            f' -H "Expect: 100-continue"'
            f' --expect100-timeout 0'
            f' --data "test body data"'
            f' http://127.0.0.1:{self.ts.Variables.port}/test/resource.js')
        client.ReturnCode = 0
        client.StartBefore(self.ts)

        # The key assertion: ATS must still be running after the test.
        # Without the fix, ATS would have crashed with a failed assertion
        # in HttpTunnel::producer_run.
        tr.StillRunningAfter = self.ts

    def runEarlyHintsRange(self, tr):
        # The range covers the entire 65 byte 308 body, as in the issue. On a
        # cache miss ATS installs the range transform and caches the
        # untransformed response, so the cache-write consumer reads from the
        # body-only server-to-transform buffer.
        client = tr.Processes.Default
        client.Command = (
            f'curl --http1.1 -s -o /dev/null'
            f' -H "Range: bytes=0-64"'
            f' http://127.0.0.1:{self.ts.Variables.port}/early-hints/redirect')
        client.ReturnCode = 0

        # Without the fix, ATS crashes with a failed assertion in
        # HttpTunnel::producer_run because the cache-write consumer skips the
        # size of the forwarded 103 headers in a buffer holding only the body.
        tr.StillRunningAfter = self.ts


CompressCacheUntransformedTest()
