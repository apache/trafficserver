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

Test.Summary = 'Test Split DNS'


class SplitDNSTest:

    def __init__(self):
        self.setupDNSServer()
        self.setupOriginServer()
        self.setupTS()

    def setupDNSServer(self):
        self.dns = Test.MakeDNServer("dns")
        self.dns.addRecords(records={'foo.ts.a.o.': ['127.0.0.1']})

    def setupOriginServer(self):
        self.origin_server = Test.MakeOriginServer("origin_server")
        self.origin_server.addResponse(
            "sessionlog.json", {"headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"},
            {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\n\r\n"})

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", select_ports=True, enable_cache=False)
        self.ts.Disk.records_config.update(
            {
                "proxy.config.dns.splitDNS.enabled": 1,
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "dns|splitdns",
            })
        self.ts.Disk.splitdns_config.AddLine(f"dest_domain=foo.ts.a.o named=127.0.0.1:{self.dns.Variables.Port}")
        self.ts.Disk.remap_config.AddLine(f"map /foo/ http://foo.ts.a.o:{self.origin_server.Variables.Port}/")
        self.ts.Disk.remap_config.AddLine(f"map /bar/ http://127.0.0.1:{self.origin_server.Variables.Port}/")

    def addTestCase0(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"curl -v http://localhost:{self.ts.Variables.port}/foo/"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/test_case_0_stderr.gold"
        tr.Processes.Default.StartBefore(self.dns)
        tr.Processes.Default.StartBefore(self.origin_server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.dns
        tr.StillRunningAfter = self.origin_server
        tr.StillRunningAfter = self.ts

    def addTestCase1(self):
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"curl -v http://localhost:{self.ts.Variables.port}/bar/"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/test_case_1_stderr.gold"
        tr.StillRunningAfter = self.dns
        tr.StillRunningAfter = self.origin_server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.addTestCase0()
        self.addTestCase1()


SplitDNSTest().run()
