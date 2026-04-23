'''
Test that set-body takes precedence over response transform plugins.
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
Test that set-body takes precedence over a response transform plugin.
When both a null_transform plugin (which registers TS_HTTP_RESPONSE_TRANSFORM_HOOK)
and set-body are active on the same transaction, the TRANSFORM_READ path in
handle_api_return() should detect internal_msg_buffer and bypass the transform,
using setup_internal_transfer() instead. The client should receive the set-body
replacement, not the transform output.
'''
Test.ContinueOnFail = True


class SetBodyTransformTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        # Origin returns 403 with sensitive body
        request_header = {"headers": "GET /transform_test HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        response_header = {
            "headers": "HTTP/1.1 403 Forbidden\r\nContent-Length: 28\r\nConnection: close\r\n\r\n",
            "body": "Sensitive: secret-key-99999!"
        }
        self.server.addResponse("sessionfile.log", request_header, response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Setup.CopyAs('rules/rule_set_body_origin_read_resp.conf', Test.RunDirectory)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.http.cache.http': 0,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|null_transform|header_rewrite',
            })

        # Load the null_transform plugin globally -- it registers
        # TS_HTTP_RESPONSE_TRANSFORM_HOOK on every transaction
        self.ts.Disk.plugin_config.AddLine(
            '{0}/.libs/null_transform.so'.format(Test.Variables.AtsBuildGoldTestsDir + '/pluginTest/header_rewrite/plugins'))

        # Also load header_rewrite with set-body at READ_RESPONSE_HDR
        self.ts.Disk.remap_config.AddLine(
            'map http://www.example.com/ http://127.0.0.1:{0}/'
            ' @plugin=header_rewrite.so @pparam={1}/rule_set_body_origin_read_resp.conf'.format(
                self.server.Variables.Port, Test.RunDirectory))

    def test_set_body_beats_transform(self):
        """Verify set-body replacement is served, not the transform output."""
        tr = Test.AddTestRun("transform: set-body takes precedence over null transform")
        tr.MakeCurlCommand(
            '-s -D - --proxy 127.0.0.1:{0} "http://www.example.com/transform_test"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        # The set-body replacement should be served
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("HTTP/1.1 403", "Expected 403 status preserved")
        tr.Processes.Default.Streams.stdout.Content += Testers.ContainsExpression("Sanitized", "Expected set-body replacement body")
        # Origin's sensitive body should NOT leak through the transform
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression(
            "secret-key", "Origin sensitive body should not leak through transform")
        tr.StillRunningAfter = self.server

    def run(self):
        self.test_set_body_beats_transform()


SetBodyTransformTest().run()
