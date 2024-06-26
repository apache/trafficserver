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
Test.testName = "SET_BODY_FROM"


class HeaderRewriteSetBodyFromTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        # Response for original transaction
        response_header = {"headers": "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n", "body": "404 Not Found"}

        # Request/response for original failed transaction
        fail_1_request_header = {"headers": "GET /fail HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", fail_1_request_header, response_header)

        # Request/response for custom body transaction
        fail_2_request_header = {"headers": "GET /404_fail HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        fail_2_response_header = {"headers": "HTTP/1.1 502 \r\nConnection: close\r\n\r\n", "body": "Fail\n"}
        self.server.addResponse("sessionfile.log", fail_2_request_header, fail_2_response_header)

        # Request/response for original Successful transaction
        success_1_request_header = {"headers": "GET /success HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        self.server.addResponse("sessionfile.log", success_1_request_header, response_header)

        # Request/response for custom body transaction
        success_2_request_header = {"headers": "GET /404 HTTP/1.1\r\nHost: www.example.com\r\n\r\n"}
        success_2_response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "body": "Custom body found\n"}
        self.server.addResponse("sessionfile.log", success_2_request_header, success_2_response_header)

        # Set header rewrite rules
        self.ts.Setup.CopyAs('rules/rule_set_body_from.conf', Test.RunDirectory)
        self.ts.Disk.remap_config.AddLine(
            """\
             map http://www.example.com/success http://127.0.0.1:{0}/success @plugin=header_rewrite.so @pparam={1}/rule_set_body_from.conf
             map http://www.example.com/fail http://127.0.0.1:{0}/fail @plugin=header_rewrite.so @pparam={1}/rule_set_body_from.conf
             map http://www.example.com/404 http://127.0.0.1:{0}/404
             map http://www.example.com/404_fail http://127.0.0.1:{0}/404_fail
             """.format(self.server.Variables.Port, Test.RunDirectory))

    def runTraffic(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/fail"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Streams.stderr = "gold/header_rewrite-set_body_from_headers_fail.gold"
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_body_fail.gold"
        tr.StillRunningAfter = self.server

        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            'curl -s -v --proxy 127.0.0.1:{0} "http://www.example.com/success"'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/header_rewrite-set_body_from_headers.gold"
        tr.Processes.Default.Streams.stdout = "gold/header_rewrite-set_body_from_body.gold"
        tr.StillRunningAfter = self.server

    def run(self):
        self.runTraffic()


HeaderRewriteSetBodyFromTest().run()
