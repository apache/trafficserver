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
            "headers": "HTTP/1.1 200 OK\r\responseHeader: unchanged\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        self.server.addResponse("sessionfile.log", request_header, response_header)

    def setUpTS(self):
        self.ts = Test.MakeATSProcess("ts_in", enable_tls=True, enable_cache=False, enable_cripts=True)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        self.ts.Setup.Copy('files/basic.cript', self.ts.Variables.CONFIGDIR)

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

    def run(self):
        self.runHeaderTest()
        if not Condition.CurlUsingUnixDomainSocket():
            self.runCertsTest()


CriptsBasicTest().run()
