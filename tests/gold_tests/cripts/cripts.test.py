'''
Test basic cripts functionality
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

# Needed if we want to use sed -i '' on macOS, but autest doesn't like that ...
# import platform

Test.testName = "cripts: basic functions"
Test.Summary = '''
Simple cripts test that sets a response header back to the client
'''
Test.ContinueOnFail = True


class CriptsBasicTest:

    def __init__(self):
        self.setUpOriginServer()
        self.setUpTS()

    def setUpOriginServer(self):
        self.server = Test.MakeOriginServer("server")

        request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        response_header = {
            "headers": "HTTP/1.1 200 OK\r\nresponseHeader: unchanged\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        self.server.addResponse("sessionfile.log", request_header, response_header)

        query_request_header = {
            "headers": "GET /path/to/resource?foo=1&bar=2&baz=3 HTTP/1.1\r\nHost: does.not.matter\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        query_response_header = {
            "headers": "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        self.server.addResponse("sessionfile.log", query_request_header, query_response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts_in", enable_tls=True, enable_cache=False, enable_cripts=True)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

        self.ts.Setup.Copy('files/basic.cript', self.ts.Variables.CONFIGDIR)
        self.ts.Setup.Copy('files/query_copy.cript', self.ts.Variables.CONFIGDIR)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.plugin.dynamic_reload_mode': 1,
                "proxy.config.ssl.server.cert.path": f"{self.ts.Variables.SSLDir}",
                "proxy.config.ssl.server.private_key.path": f"{self.ts.Variables.SSLDir}",
            })

        self.ts.Disk.remap_config.AddLine(
            f'map http://www.example.com http://127.0.0.1:{self.server.Variables.Port} @plugin=basic.cript')
        self.ts.Disk.remap_config.AddLine(
            f'map https://www.example.com:{self.ts.Variables.ssl_port} http://127.0.0.1:{self.server.Variables.Port} @plugin=basic.cript'
        )
        self.ts.Disk.remap_config.AddLine(
            f'map http://query.example.com http://127.0.0.1:{self.server.Variables.Port} @plugin=query_copy.cript')

    def runHeaderTest(self):
        tr = Test.AddTestRun('Exercise traffic through cripts.')
        tr.MakeCurlCommand(f'-v -H "Host: www.example.com" http://127.0.0.1:{self.ts.Variables.port}', ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.server, ready=When.PortOpen(self.server.Variables.Port))
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Streams.stderr = "gold/basic_cript.gold"
        tr.StillRunningAfter = self.server

    def runCertsTest(self):
        tr = Test.AddTestRun('Exercise Cripts certificate introspection.')
        tr.MakeCurlCommand(
            f'-v --http1.1 -k -H "Host: www.example.com:{self.ts.Variables.ssl_port}" https://127.0.0.1:{self.ts.Variables.ssl_port}',
            ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/certs_cript.gold"
        tr.StillRunningAfter = self.server

    def runQueryCopyTest(self):
        tr = Test.AddTestRun('Exercise Query/Path copy ctor / copy-assign in cripts.')
        tr.MakeCurlCommand(
            f'-v -H "Host: query.example.com" "http://127.0.0.1:{self.ts.Variables.port}/path/to/resource?foo=1&bar=2&baz=3"',
            ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        # Live URL is untouched: original query is preserved.
        tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(
            r"X-Original-Query: foo=1&bar=2&baz=3", "Original query must round-trip unchanged")
        # Mutating the copy (Erase "foo") must not have leaked into the original.
        tr.Processes.Default.Streams.stderr += Testers.ContainsExpression(
            r"X-Copy-Query: bar=2&baz=3", "Erase on the copy must drop foo without affecting the original")
        # Copy-assignment path produces a snapshot equal to the original.
        tr.Processes.Default.Streams.stderr += Testers.ContainsExpression(
            r"X-Assigned-Query: foo=1&bar=2&baz=3", "Copy-assigned query must equal the original")
        # Live URL path is untouched: original path is preserved.
        tr.Processes.Default.Streams.stderr += Testers.ContainsExpression(
            r"X-Original-Path: path/to/resource", "Original path must round-trip unchanged")
        # Mutating the copy (segment[0] = "edited") must not have leaked into the original.
        tr.Processes.Default.Streams.stderr += Testers.ContainsExpression(
            r"X-Copy-Path: edited/to/resource", "Segment edit on the copy must not affect the original")
        # Copy-assignment path produces a snapshot equal to the original.
        tr.Processes.Default.Streams.stderr += Testers.ContainsExpression(
            r"X-Assigned-Path: path/to/resource", "Copy-assigned path must equal the original")
        tr.StillRunningAfter = self.server

    def run(self):
        self.runHeaderTest()
        self.runQueryCopyTest()
        if not Condition.CurlUsingUnixDomainSocket():
            self.runCertsTest()


CriptsBasicTest().run()
