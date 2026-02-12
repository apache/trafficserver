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
Test set-body-from replacing origin response bodies with content fetched from
a secondary URL. Covers READ_RESPONSE_HDR_HOOK, SEND_RESPONSE_HDR_HOOK, and
fetch failure scenarios. Verifies that the origin status code and headers are
preserved while only the body is replaced.
'''
Test.ContinueOnFail = True

# Note: This test uses MakeOriginServer rather than ATSReplayTest because
# set-body-from uses TSFetchUrl internally, which creates a secondary HTTP
# request back through ATS. The replay framework's client-driven model doesn't
# handle these internal multi-hop requests cleanly.


class HeaderRewriteSetBodyFromTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        # -- Custom body endpoint (served when set-body-from fetch succeeds) --
        custom_body_request = {"headers": "GET /custom_body HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        custom_body_response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "Custom body found\n"}
        self.server.addResponse("sessionfile.log", custom_body_request, custom_body_response)

        # -- Primary endpoints for READ_RESPONSE_HDR tests --
        # Origin returns 404 (body should be replaced by custom_body)
        read_resp_404_request = {"headers": "GET /read_resp_404 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        read_resp_404_response = {"headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n", "body": "Original 404 body"}
        self.server.addResponse("sessionfile.log", read_resp_404_request, read_resp_404_response)

        # Origin returns 200 (body should be replaced by custom_body)
        read_resp_200_request = {"headers": "GET /read_resp_200 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        read_resp_200_response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "Original 200 body"}
        self.server.addResponse("sessionfile.log", read_resp_200_request, read_resp_200_response)

        # -- Primary endpoints for SEND_RESPONSE_HDR tests --
        # Origin returns 404 (body should be replaced by custom_body)
        send_resp_404_request = {"headers": "GET /send_resp_404 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        send_resp_404_response = {"headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n", "body": "Original 404 body"}
        self.server.addResponse("sessionfile.log", send_resp_404_request, send_resp_404_response)

        # Origin returns 200 (body should be replaced by custom_body)
        send_resp_200_request = {"headers": "GET /send_resp_200 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        send_resp_200_response = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "Original 200 body"}
        self.server.addResponse("sessionfile.log", send_resp_200_request, send_resp_200_response)

        # -- Primary endpoint for fetch failure test --
        # Origin returns 404 (body should NOT be replaced because fetch fails)
        fetch_fail_request = {"headers": "GET /fetch_fail HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        fetch_fail_response = {"headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n", "body": "Original 404 body"}
        self.server.addResponse("sessionfile.log", fetch_fail_request, fetch_fail_response)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Setup.CopyAs('rules/rule_set_body_from_read_resp.conf', Test.RunDirectory)
        self.ts.Setup.CopyAs('rules/rule_set_body_from_send_resp.conf', Test.RunDirectory)
        self.ts.Setup.CopyAs('rules/rule_set_body_from_read_resp_fail.conf', Test.RunDirectory)

        self.ts.Disk.remap_config.AddLines(
            [
                # Primary endpoints with set-body-from at READ_RESPONSE_HDR_HOOK
                'map http://www.example.com/read_resp_404'
                ' http://127.0.0.1:{0}/read_resp_404'
                ' @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_read_resp.conf'.format(
                    self.server.Variables.Port, Test.RunDirectory),
                'map http://www.example.com/read_resp_200'
                ' http://127.0.0.1:{0}/read_resp_200'
                ' @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_read_resp.conf'.format(
                    self.server.Variables.Port, Test.RunDirectory),

                # Primary endpoints with set-body-from at SEND_RESPONSE_HDR_HOOK
                'map http://www.example.com/send_resp_404'
                ' http://127.0.0.1:{0}/send_resp_404'
                ' @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_send_resp.conf'.format(
                    self.server.Variables.Port, Test.RunDirectory),
                'map http://www.example.com/send_resp_200'
                ' http://127.0.0.1:{0}/send_resp_200'
                ' @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_send_resp.conf'.format(
                    self.server.Variables.Port, Test.RunDirectory),

                # Primary endpoint for fetch failure test (fetch URL goes to bad port)
                'map http://www.example.com/fetch_fail'
                ' http://127.0.0.1:{0}/fetch_fail'
                ' @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_read_resp_fail.conf'.format(
                    self.server.Variables.Port, Test.RunDirectory),

                # Fetch URL endpoint (served for successful set-body-from fetches)
                'map http://www.example.com/custom_body http://127.0.0.1:{0}/custom_body'.format(self.server.Variables.Port),

                # Fetch failure URL maps to a port with no server (will cause fetch failure)
                'map http://www.example.com/no_server http://127.0.0.1:{0}/no_server'.format(Test.GetTcpPort("bad_port")),
            ])

    # Test 1: READ_RESPONSE_HDR + origin 404, fetch succeeds -> body replaced, 404 preserved
    def test_read_resp_404_fetch_succeeds(self):
        tr = Test.AddTestRun("read_resp_404: fetch succeeds, body replaced, 404 preserved")
        tr.MakeCurlCommand(
            '-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/read_resp_404"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("HTTP/1.1 404", "Expected 404 status")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression("Custom body found", "Expected custom body")
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "Original 404 body", "Original body should be replaced")
        tr.StillRunningAfter = self.server

    # Test 2: READ_RESPONSE_HDR + origin 200, fetch succeeds -> body replaced, 200 preserved
    def test_read_resp_200_fetch_succeeds(self):
        tr = Test.AddTestRun("read_resp_200: fetch succeeds, body replaced, 200 preserved")
        tr.MakeCurlCommand(
            '-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/read_resp_200"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("HTTP/1.1 200", "Expected 200 status")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression("Custom body found", "Expected custom body")
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "Original 200 body", "Original body should be replaced")
        tr.StillRunningAfter = self.server

    # Test 3: SEND_RESPONSE_HDR + origin 404, fetch succeeds -> body replaced, 404 preserved
    def test_send_resp_404_fetch_succeeds(self):
        tr = Test.AddTestRun("send_resp_404: fetch succeeds, body replaced, 404 preserved")
        tr.MakeCurlCommand(
            '-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/send_resp_404"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("HTTP/1.1 404", "Expected 404 status")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression("Custom body found", "Expected custom body")
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "Original 404 body", "Original body should be replaced")
        tr.StillRunningAfter = self.server

    # Test 4: SEND_RESPONSE_HDR + origin 200, fetch succeeds -> body replaced, 200 preserved
    def test_send_resp_200_fetch_succeeds(self):
        tr = Test.AddTestRun("send_resp_200: fetch succeeds, body replaced, 200 preserved")
        tr.MakeCurlCommand(
            '-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/send_resp_200"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("HTTP/1.1 200", "Expected 200 status")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression("Custom body found", "Expected custom body")
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "Original 200 body", "Original body should be replaced")
        tr.StillRunningAfter = self.server

    # Test 5: READ_RESPONSE_HDR + fetch backend unreachable -> ATS error page replaces body, status preserved
    # When the fetch URL's backend is unreachable, ATS returns its own error page to the FetchSM.
    # The FetchSM treats this as a successful fetch and replaces the body with the error page.
    # The original origin status code (404) is preserved.
    def test_fetch_backend_unreachable(self):
        tr = Test.AddTestRun("fetch_fail: backend unreachable, ATS error page replaces body, 404 preserved")
        tr.MakeCurlCommand(
            '-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/fetch_fail"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
            "HTTP/1.1 404", "Expected original 404 status preserved")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression(
            "Could Not Connect", "Expected ATS error page from unreachable fetch backend")
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "Original 404 body", "Original body should be replaced by ATS error page")
        tr.StillRunningAfter = self.server

    def run(self):
        self.test_read_resp_404_fetch_succeeds()
        self.test_read_resp_200_fetch_succeeds()
        self.test_send_resp_404_fetch_succeeds()
        self.test_send_resp_200_fetch_succeeds()
        self.test_fetch_backend_unreachable()


HeaderRewriteSetBodyFromTest().run()
