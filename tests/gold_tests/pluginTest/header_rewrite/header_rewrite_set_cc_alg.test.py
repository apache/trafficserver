'''
Test header_rewrite option set-cc-alg.
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
import os

Test.Summary = '''
Test header_rewrite option set-cc-alg.
'''

Test.ContinueOnFail = True
Test.SkipUnless(Condition.IsPlatform("linux"))


class HeaderRewriteSetCCAlgTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        Test.testName = ""
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        request_header = {"headers": "GET /cc HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        self.server.addResponse("sessionfile.log", request_header, response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'dbg_header_rewrite',
            })

        self.ts.Setup.CopyAs('rules/set-cc-alg_valid.conf', Test.RunDirectory)
        self.ts.Setup.CopyAs('rules/set-cc-alg_invalid.conf', Test.RunDirectory)

        self.ts.Disk.remap_config.AddLine(
            'map http://www.example.com/valid http://127.0.0.1:{0}/ @plugin=header_rewrite.so @pparam={1}/set-cc-alg_valid.conf'
            .format(self.server.Variables.Port, Test.RunDirectory))
        self.ts.Disk.remap_config.AddLine(
            'map http://www.example.com/invalid http://127.0.0.1:{0}/ @plugin=header_rewrite.so @pparam={1}/set-cc-alg_invalid.conf'
            .format(self.server.Variables.Port, Test.RunDirectory))

    def test_validAlg(self):
        tr = Test.AddTestRun()
        tr.MakeCurlCommand('--proxy 127.0.0.1:{0} "http://www.example.com/valid" --verbose'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server, ready=When.PortOpen(self.server.Variables.Port))
        tr.Processes.Default.StartBefore(Test.Processes.ts)
        self.ts.Disk.diags_log.Content = Testers.ExcludesExpression("ERROR", "Should contain an error message")
        tr.StillRunningAfter = self.server

    def test_invalidAlg(self):
        tr = Test.AddTestRun()
        tr.MakeCurlCommand('--proxy 127.0.0.1:{0} "http://www.example.com/invalid" --verbose'.format(self.ts.Variables.port))
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server, ready=When.PortOpen(self.server.Variables.Port))
        tr.Processes.Default.StartBefore(Test.Processes.ts)
        tr.StillRunningAfter = self.server
        self.ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "Should contain an error message")

    def runTraffic(self):
        self.test_validAlg()
        self.test_invalidAlg()

    def run(self):
        self.runTraffic()


HeaderRewriteSetCCAlgTest().run()
