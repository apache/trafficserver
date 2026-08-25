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
Test for successful response manipulation using set-body-from
'''
Test.ContinueOnFail = True


class HeaderRewriteSetBodyFromTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        # Response for original transaction
        response_header = {"headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n", "body": "404 Not Found"}

        # Request/response for original transaction where transaction returns a 200 status code
        remap_success_request_header = {"headers": "GET /200 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        ooo = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "200 OK"}

        self.server.addResponse("sessionfile.log", remap_success_request_header, ooo)

        # Request/response for original transaction with failed second tranasaction
        remap_fail_1_request_header = {"headers": "GET /remap_fail HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", remap_fail_1_request_header, response_header)

        plugin_fail_1_request_header = {"headers": "GET /plugin_fail HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", plugin_fail_1_request_header, response_header)

        # Request/response for original successful transaction with successful second tranasaction
        remap_success_1_request_header = {"headers": "GET /remap_success HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", remap_success_1_request_header, response_header)

        plugin_success_1_request_header = {"headers": "GET /plugin_success HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", plugin_success_1_request_header, response_header)

        # Request/response for custom body transaction that successfully retrieves body
        success_2_request_header = {"headers": "GET /404.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        success_2_response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "Custom body found\n"}
        self.server.addResponse("sessionfile.log", success_2_request_header, success_2_response_header)

        # Request/response for original transaction that triggers binary set-body-from
        remap_binary_request_header = {"headers": "GET /remap_binary HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", remap_binary_request_header, response_header)

        # Response for the set-body-from fetch: body has an internal NUL. The strdup
        # bug truncated everything after the NUL and emitted heap garbage instead.
        binary_body_request_header = {"headers": "GET /binary_body HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        binary_body_response_header = {
            "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
            "body": "BeforeNUL\x00AfterNUL_AAAAAAAAAAAAAAAAAAAAAAAA"
        }
        self.server.addResponse("sessionfile.log", binary_body_request_header, binary_body_response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        # Set header rewrite rules
        self.ts.Setup.CopyAs('rules/rule_set_body_from_remap.conf', Test.RunDirectory)
        self.ts.Setup.CopyAs('rules/rule_set_body_from_plugin.conf', Test.RunDirectory)

        self.ts.Disk.remap_config.AddLine(
            """\
             map http://www.example.com/remap_success http://127.0.0.1:{0}/remap_success @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_remap.conf
             map http://www.example.com/200 http://127.0.0.1:{0}/200 @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_remap.conf
             map http://www.example.com/remap_fail http://127.0.0.1:{0}/remap_fail @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_remap.conf
             map http://www.example.com/remap_binary http://127.0.0.1:{0}/remap_binary @plugin=header_rewrite.so @pparam={1}/rule_set_body_from_remap.conf
             map http://www.example.com/binary_body http://127.0.0.1:{0}/binary_body
             map http://www.example.com/plugin_success http://127.0.0.1:{0}/plugin_success
             map http://www.example.com/plugin_fail http://127.0.0.1:{0}/plugin_fail
             map http://www.example.com/404.html http://127.0.0.1:{0}/404.html
             map http://www.example.com/plugin_no_server http://127.0.0.1::{2}/plugin_no_server
             """.format(self.server.Variables.Port, Test.RunDirectory, Test.GetTcpPort("bad_port")))
        self.ts.Disk.plugin_config.AddLine('header_rewrite.so {0}/rule_set_body_from_plugin.conf'.format(Test.RunDirectory))

    def test_setBodyFromFails_remap(self):
        '''
        Test where set-body-from request fails
        Triggered from remap file
        This uses the case where no remap rule is provided
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(
            '-s -v --proxy 127.0.0.1:{0} "http://www.example.com/remap_fail"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_remap_fail.gold"
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("404 Not Found", "Expected 404 response")
        tr.StillRunningAfter = self.server

    def test_setBodyFromSucceeds_remap(self):
        '''
        Test where set-body-from request succeeds
        Triggered from remap file
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(
            '-s -v --proxy 127.0.0.1:{0} "http://www.example.com/remap_success"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_success.gold"
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("404 Not Found", "Expected 404 response")
        tr.StillRunningAfter = self.server

    def test_setBodyFromSucceeds_plugin(self):
        '''
        Test where set-body-from request succeeds
        Triggered from plugin file
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(
            '-s -v --proxy 127.0.0.1:{0} "http://www.example.com/plugin_success"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_success.gold"
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("404 Not Found", "Expected 404 response")
        tr.StillRunningAfter = self.server

    def test_setBodyFromFails_plugin(self):
        '''
        Test where set-body-from request fails
        This uses the case where the second endpoint cannot connect to the requested server
        Triggered from plugin file
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand(
            '-s -v --proxy 127.0.0.1:{0} "http://www.example.com/plugin_fail"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_conn_fail.gold"
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("404 Not Found", "Expected 404 response")
        tr.StillRunningAfter = self.server

    def test_setBodyFromSucceeds_200(self):
        '''
        Test where set-body-from request succeeds and returns 200 OK
        Triggered from remap file
        This is tested because right now, TSHttpTxnErrorBodySet will change OK status codes to 500 INKApi Error
        Ideally, this would not occur.
        '''
        tr = Test.AddTestRun()
        tr.MakeCurlCommand('-s -v --proxy 127.0.0.1:{0} "http://www.example.com/200"'.format(self.ts.Variables.port), ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_200.gold"
        tr.Processes.Default.Streams.stderr.Content = Testers.ContainsExpression("500 INKApi Error", "Expected 500 response")
        tr.StillRunningAfter = self.server

    def test_setBodyFromBinary(self):
        '''
        set-body-from must preserve binary bodies that contain internal NUL
        bytes. The previous TSstrdup path truncated at the first NUL and
        emitted heap bytes for the remainder; check the SHA256 of the body
        the client actually receives.
        '''
        body_path = f"{Test.RunDirectory}/binary_body.out"
        # SHA256 of b"BeforeNUL\x00AfterNUL_AAAAAAAAAAAAAAAAAAAAAAAA" (43 bytes)
        expected_sha = "a529bbd61061b739b611ca67a7b76fc433b3d3a9cc3bcc2e385b812f00b0fe63"

        tr = Test.AddTestRun()
        tr.MakeCurlCommand(
            f'-s --proxy 127.0.0.1:{self.ts.Variables.port} -o {body_path} "http://www.example.com/remap_binary"', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server

        # Compute the hash via Python instead of sha256sum/shasum so the
        # check does not depend on which CLI hash tool the host happens
        # to ship.
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            f'python3 -c \'import hashlib; '
            f'print(hashlib.sha256(open("{body_path}", "rb").read()).hexdigest())\'')
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout.Content = Testers.ContainsExpression(
            expected_sha, "Client must receive the exact binary body bytes")

    def runTraffic(self):
        self.test_setBodyFromFails_remap()
        self.test_setBodyFromSucceeds_remap()
        self.test_setBodyFromSucceeds_plugin()
        self.test_setBodyFromFails_plugin()
        self.test_setBodyFromSucceeds_200()
        self.test_setBodyFromBinary()

    def run(self):
        self.runTraffic()


HeaderRewriteSetBodyFromTest().run()
