'''
Test splitdns.config reload via ConfigRegistry.

Verifies that:
1. splitdns.config reload works after file touch
2. The reload handler is invoked (diags log check)
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

Test.Summary = 'Test splitdns.config reload via ConfigRegistry.'
Test.ContinueOnFail = True


class SplitDNSReloadTest:

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
        self.ts = Test.MakeATSProcess("ts", enable_cache=False)
        self.ts.Disk.records_config.update(
            {
                'proxy.config.dns.splitDNS.enabled': 1,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'splitdns|config',
            })
        self.ts.Disk.splitdns_config.AddLine(f"dest_domain=foo.ts.a.o named=127.0.0.1:{self.dns.Variables.Port}")
        self.ts.Disk.remap_config.AddLine(f"map /foo/ http://foo.ts.a.o:{self.origin_server.Variables.Port}/")

    def run(self):
        config_dir = self.ts.Variables.CONFIGDIR

        # Test 1: Verify basic SplitDNS works (startup loads config)
        tr = Test.AddTestRun("Verify SplitDNS works at startup")
        tr.MakeCurlCommand(f"-v http://localhost:{self.ts.Variables.port}/foo/", ts=self.ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.dns)
        tr.Processes.Default.StartBefore(self.origin_server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.ts

        # Test 2: Touch splitdns.config -> reload -> handler fires
        tr = Test.AddTestRun("Touch splitdns.config")
        tr.Processes.Default.Command = (f"touch {os.path.join(config_dir, 'splitdns.config')} && sleep 1")
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.ts

        tr = Test.AddTestRun("Reload after splitdns.config touch")
        p = tr.Processes.Process("reload-1")
        p.Command = 'traffic_ctl config reload; sleep 30'
        p.Env = self.ts.Env
        p.ReturnCode = Any(0, -2)
        # Wait for 2nd "finished loading" (1st is startup)
        p.Ready = When.FileContains(self.ts.Disk.diags_log.Name, "splitdns.config finished loading", 2)
        p.Timeout = 20
        tr.Processes.Default.StartBefore(p)
        tr.Processes.Default.Command = ('echo "waiting for splitdns.config reload"')
        tr.TimeOut = 25
        tr.StillRunningAfter = self.ts


SplitDNSReloadTest().run()
