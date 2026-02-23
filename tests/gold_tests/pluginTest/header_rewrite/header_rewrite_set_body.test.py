'''
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
Test for empty set-body operator value.
'''

Test.ContinueOnFail = True


class HeaderRewriteSetBodyTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        # Response for original transaction
        request_header = {"headers": "GET /test HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "body": ""}

        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "ATS should not serve this body"}
        self.server.addResponse("sessionlog.log", request_header, response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")
        empty_body_rule_file = Test.Disk.File("empty_body_rule.conf", "txt", "")
        empty_body_rule_file.WriteOn(
            '''
                cond %{REMAP_PSEUDO_HOOK}
                    set-status 200

                cond %{SEND_RESPONSE_HDR_HOOK}
                    set-body ""
            ''')
        self.ts.Disk.remap_config.AddLine(
            f'map http://www.example.com/emptybody http://127.0.0.1:{self.server.Variables.Port}/test @plugin=header_rewrite.so @pparam={empty_body_rule_file.AbsRunTimePath}'
        )

        set_body_rule_file = Test.Disk.File("set_body_rule.conf", "txt", "")
        set_body_rule_file.WriteOn(
            '''
                cond %{REMAP_PSEUDO_HOOK}
                    set-status 200

                cond %{SEND_RESPONSE_HDR_HOOK}
                    set-body "%{STATUS}"
            ''')
        self.ts.Disk.remap_config.AddLine(
            f'map http://www.example.com/setbody http://127.0.0.1:{self.server.Variables.Port}/test @plugin=header_rewrite.so @pparam={set_body_rule_file.AbsRunTimePath}'
        )

        # Enable debug logging to help diagnose issues
        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|header_rewrite',
            })

    def test_empty_body(self):
        '''
        Test that empty set-body doesn't crash the server and properly deletes internal error body
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(f'-s -v --proxy 127.0.0.1:{self.ts.Variables.port} "http://www.example.com/emptybody"', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("200 OK", "expected 200 response")
        tr.Processes.Default.Streams.stderr.Content += Testers.ContainsExpression("Content-Length: 0", "expected content-length 0")
        tr.Processes.Default.Streams.stdout.Content = Testers.ExcludesExpression("should not", "body should be removed")

        tr.StillRunningAfter = self.ts  # Verify server didn't crash

    def test_set_body(self):
        '''
        Test that set-body with a variable works correctly
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(f'-s -v --proxy 127.0.0.1:{self.ts.Variables.port} "http://www.example.com/setbody"', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("200 OK", "expected 200 response")
        tr.Processes.Default.Streams.stderr.Content += Testers.ContainsExpression("Content-Length: 3", "expected content-length 3")
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression("200", "body should be set to 200")
        tr.Processes.Default.Streams.stdout.Content += Testers.ExcludesExpression("should not", "body should be removed")

        tr.StillRunningAfter = self.ts  # Verify server didn't crash

    def run(self):
        self.test_empty_body()
        self.test_set_body()


HeaderRewriteSetBodyTest().run()
