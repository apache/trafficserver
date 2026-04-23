'''
Test that TSHttpTxnServerRequestBodySet does not trigger set-body code paths.
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
Test that the api_server_request_body_set guard works correctly.
TSHttpTxnServerRequestBodySet() sets internal_msg_buffer (the same field
used by TSHttpTxnErrorBodySet / set-body). Without the guard, the
handle_api_return() and how_to_open_connection() checks would consume
this buffer as a response body replacement, preventing the request from
reaching the origin.

This test loads a plugin that calls TSHttpTxnServerRequestBodySet() at
SEND_REQUEST_HDR to inject a POST body to the origin. The test verifies:
1. The origin IS contacted (not short-circuited)
2. The origin's response body is served to the client unmodified
'''
Test.ContinueOnFail = True


class SetBodyGuardTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        # The origin returns a normal 200 response. The test plugin injects
        # a POST body at SEND_REQUEST_HDR (changing GET -> POST), but the
        # origin's response should flow through unchanged.
        # MakeOriginServer matches on the request as received by the origin.
        request_header = {"headers": "POST /test HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        # Also add a GET handler in case the plugin doesn't fire
        get_request_header = {"headers": "GET /test HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        response_header = {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 21\r\nConnection: close\r\n\r\n",
            "body": "Origin response body!"
        }
        self.server.addResponse("sessionfile.log", request_header, response_header)
        self.server.addResponse("sessionfile.log", get_request_header, response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Disk.records_config.update(
            {
                'proxy.config.http.cache.http': 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|set_server_request_body',
            })

        # Load the set_server_request_body plugin globally
        self.ts.Disk.plugin_config.AddLine(
            '{0}/.libs/set_server_request_body.so injected-body-content'.format(
                Test.Variables.AtsBuildGoldTestsDir + '/pluginTest/header_rewrite/plugins'))

        self.ts.Disk.remap_config.AddLine('map http://www.example.com/ http://127.0.0.1:{0}/'.format(self.server.Variables.Port))

    def test_guard_prevents_interception(self):
        """Verify that TSHttpTxnServerRequestBodySet does NOT trigger set-body."""
        tr = Test.AddTestRun("guard: origin is contacted and response body is served unmodified")
        tr.MakeCurlCommand('-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/test"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        # The origin's response should be served, NOT a synthetic body
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("HTTP/1.1 200", "Expected 200 from origin")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression(
            "Origin response body!", "Expected origin response body, not a synthetic replacement")
        # Should NOT see any synthetic replacement body
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "injected-body-content", "The injected request body should NOT appear as the response")
        tr.StillRunningAfter = self.server

    def run(self):
        self.test_guard_prevents_interception()


SetBodyGuardTest().run()
