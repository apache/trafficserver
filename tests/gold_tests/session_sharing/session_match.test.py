'''
Test that a plugin can modify server session sharing.
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


class SessionMatchTest:
    TestCounter = 0

    def __init__(self, TestSummary, sharingMatchValue):
        SessionMatchTest.TestCounter += 1
        self._MyTestCount = SessionMatchTest.TestCounter
        Test.Summary = TestSummary
        self._tr = Test.AddTestRun()
        self._sharingMatchValue = sharingMatchValue
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self._server = Test.MakeOriginServer("server{counter}".format(counter=self._MyTestCount))
        request_header = {"headers":
                          "GET /one HTTP/1.1\r\nHost: www.example.com\r\nContent-Length: 0\r\n\r\n",
                          "timestamp": "1469733493.993", "body": ""}
        response_header = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                           "Content-Length: 0\r\n\r\n",
                           "timestamp": "1469733493.993", "body": ""}
        self._server.addResponse("sessionlog.json", request_header, response_header)

        request_header2 = {"headers": "GET /two HTTP/1.1\r\nContent-Length: 0\r\n"
                           "Host: www.example.com\r\n\r\n",
                           "timestamp": "1469733493.993", "body": "a\r\na\r\na\r\n\r\n"}
        response_header2 = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                            "Content-Length: 0\r\n\r\n",
                            "timestamp": "1469733493.993", "body": ""}
        self._server.addResponse("sessionlog.json", request_header2, response_header2)

        request_header3 = {"headers": "GET /three HTTP/1.1\r\nContent-Length: 0\r\n"
                           "Host: www.example.com\r\nConnection: close\r\n\r\n",
                           "timestamp": "1469733493.993", "body": "a\r\na\r\na\r\n\r\n"}
        response_header3 = {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\n"
                            "Connection: close\r\nContent-Length: 0\r\n\r\n",
                            "timestamp": "1469733493.993", "body": ""}
        self._server.addResponse("sessionlog.json", request_header3, response_header3)

    def setupTS(self):
        self._ts = Test.MakeATSProcess("ts{counter}".format(counter=self._MyTestCount))
        self._ts.Disk.remap_config.AddLine(
            'map / http://127.0.0.1:{0}'.format(self._server.Variables.Port)
        )
        self._ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http',
            'proxy.config.http.auth_server_session_private': 1,
            'proxy.config.http.server_session_sharing.pool': 'global',
            'proxy.config.http.server_session_sharing.match': self._sharingMatchValue,
        })

    def _runTraffic(self):
        self._tr.Processes.Default.Command = (
            'curl -v -H\'Host: www.example.com\' -H\'Connection: close\' http://127.0.0.1:{port}/one &&'
            'curl -v -H\'Host: www.example.com\' -H\'Connection: close\' http://127.0.0.1:{port}/two &&'
            'curl -v -H\'Host: www.example.com\' -H\'Connection: close\' http://127.0.0.1:{port}/three'.format(
                port=self._ts.Variables.port))
        self._tr.Processes.Default.ReturnCode = 0
        self._tr.Processes.Default.StartBefore(self._server)
        self._tr.Processes.Default.StartBefore(self._ts)
        self._tr.Processes.Default.Streams.stderr = "gold/200.gold"

    def runAndExpectSharing(self):
        self._runTraffic()
        self._ts.Disk.traffic_out.Content = Testers.ContainsExpression(
            "global pool search successful",
            "Verify that sessions got shared")

    def runAndExpectNoSharing(self):
        self._runTraffic()
        self._ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "global pool search successful",
            "Verify that sessions did not get shared")


sessionMatchTest = SessionMatchTest(
    TestSummary='Test that session sharing works with host matching',
    sharingMatchValue='host')
sessionMatchTest.runAndExpectSharing()

sessionMatchTest = SessionMatchTest(
    TestSummary='Test that session sharing works with ip matching',
    sharingMatchValue='ip')
sessionMatchTest.runAndExpectSharing()

sessionMatchTest = SessionMatchTest(
    TestSummary='Test that session sharing works with matching both ip and host',
    sharingMatchValue='both')
sessionMatchTest.runAndExpectSharing()

sessionMatchTest = SessionMatchTest(
    TestSummary='Test that session sharing is disabled when matching is set to none',
    sharingMatchValue='none')
sessionMatchTest.runAndExpectNoSharing()
