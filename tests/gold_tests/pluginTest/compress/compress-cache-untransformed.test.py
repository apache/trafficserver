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
Regression test for compress plugin with cache=true causing assertion failure
when origin sends 100 Continue before a compressible response (#12244)
'''

Test.SkipUnless(Condition.PluginExists('compress.so'))


class CompressCacheUntransformedTest:

    def __init__(self):
        self.setupTS()
        self.run()

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_cache=True)

    def run(self):
        tr = Test.AddTestRun()

        # Copy scripts into the test run directory.
        tr.Setup.CopyAs("compress_100_continue_origin.py")
        tr.Setup.Copy("etc/compress-cache-false.config")

        # Create and configure the custom origin server process.
        origin = tr.Processes.Process("origin")
        origin_port = get_port(origin, 'http_port')
        origin.Command = (f'{sys.executable} compress_100_continue_origin.py'
                          f' --port {origin_port}')
        origin.Ready = When.PortOpenv4(origin_port)
        origin.ReturnCode = 0

        # Configure ATS.
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|compress|http_tunnel",
                # Do NOT send 100 Continue from ATS - let the origin send it.
                # This ensures ATS processes the origin's 100 via
                # handle_100_continue_response -> setup_100_continue_transfer,
                # which sets client_response_hdr_bytes.
                "proxy.config.http.send_100_continue_response": 0,
                # Enable POST caching so that the 200 OK is cached, triggering
                # the cache write path where the stale client_response_hdr_bytes
                # causes the crash.
                "proxy.config.http.cache.post_method": 1,
            })

        self.ts.Disk.remap_config.AddLine(
            f'map / http://127.0.0.1:{origin_port}/'
            f' @plugin=compress.so'
            f' @pparam={Test.RunDirectory}/compress-cache-false.config')

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
        client.StartBefore(origin)
        client.StartBefore(self.ts)

        # The key assertion: ATS must still be running after the test.
        # Without the fix, ATS would have crashed with a failed assertion
        # in HttpTunnel::producer_run.
        tr.StillRunningAfter = self.ts


CompressCacheUntransformedTest()
