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

        # Request/response for custom body transaction that fails to retrieve body
        fail_2_request_header = {"headers": "GET /502.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        fail_2_response_header = {"headers": "HTTP/1.1 502 \r\nConnection: close\r\n\r\n", "body": "Fail\n"}
        self.server.addResponse("sessionfile.log", fail_2_request_header, fail_2_response_header)

        # Request/response for custom body transaction that successfully retrieves body
        success_2_request_header = {"headers": "GET /404.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        success_2_response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "Custom body found\n"}
        self.server.addResponse("sessionfile.log", success_2_request_header, success_2_response_header)

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
             map http://www.example.com/plugin_success http://127.0.0.1:{0}/plugin_success
             map http://www.example.com/plugin_fail http://127.0.0.1:{0}/plugin_fail
             map http://www.example.com/404.html http://127.0.0.1:{0}/404.html
             map http://www.example.com/502.html http://127.0.0.1:{0}/502.html
             """.format(self.server.Variables.Port, Test.RunDirectory))
        self.ts.Disk.plugin_config.AddLine('header_rewrite.so {0}/rule_set_body_from_plugin.conf'.format(Test.RunDirectory))

    def test_setBodyFromFails_remap(self):
        '''
        Test where set-body-from request fails
        Triggered from remap file
        '''
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/remap_fail"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Streams.All = "gold/header_rewrite-set_body_from_fail.gold"
        tr.StillRunningAfter = self.server

    def test_setBodyFromSucceeds_remap(self):
        '''
        Test where set-body-from request succeeds
        Triggered from remap file
        '''
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/remap_success"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = "gold/header_rewrite-set_body_from_success.gold"
        tr.StillRunningAfter = self.server

    def test_setBodyFromSucceeds_plugin(self):
        '''
        Test where set-body-from request succeeds
        Triggered from plugin file
        '''
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/plugin_success"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = "gold/header_rewrite-set_body_from_success.gold"
        tr.StillRunningAfter = self.server

    def test_setBodyFromFails_plugin(self):
        '''
        Test where set-body-from request fails
        Triggered from plugin file
        '''
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/plugin_fail"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = "gold/header_rewrite-set_body_from_fail.gold"
        tr.StillRunningAfter = self.server

    def test_setBodyFromSucceeds_200(self):
        '''
        Test where set-body-from request succeeds and returns 200 OK
        Triggered from remap file
        This is tested because right now, TSHttpTxnErrorBodySet will change OK status codes to 500 INKApi Error
        Ideally, this would not occur.
        '''
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/200"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.All = "gold/header_rewrite-set_body_from_200.gold"
        tr.StillRunningAfter = self.server

    def runTraffic(self):
        self.test_setBodyFromFails_remap()
        self.test_setBodyFromSucceeds_remap()
        self.test_setBodyFromSucceeds_plugin()
        self.test_setBodyFromFails_plugin()
        self.test_setBodyFromSucceeds_200()

    def run(self):
        self.runTraffic()


HeaderRewriteSetBodyFromTest().run()
