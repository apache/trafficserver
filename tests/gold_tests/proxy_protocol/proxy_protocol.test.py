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

import sys

Test.Summary = 'Test PROXY Protocol'
Test.SkipUnless(
    Condition.HasCurlOption("--haproxy-protocol")
)
Test.ContinueOnFail = True


class ProxyProtocolTest:
    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.httpbin = Test.MakeHttpBinServer("httpbin")
        # TODO: when httpbin 0.8.0 or later is released, remove below json pretty print hack
        self.json_printer = f'''
{sys.executable} -c "import sys,json; print(json.dumps(json.load(sys.stdin), indent=2, separators=(',', ': ')))"
'''

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True, enable_cache=False)

        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")

        self.ts.Disk.remap_config.AddLine(
            f"map / http://127.0.0.1:{self.httpbin.Variables.Port}/")

        self.ts.Disk.records_config.update({
            "proxy.config.http.server_ports": f"{self.ts.Variables.port}:pp {self.ts.Variables.ssl_port}:ssl:pp",
            "proxy.config.http.proxy_protocol_allowlist": "127.0.0.1",
            "proxy.config.http.insert_forwarded": "for|proto",
            "proxy.config.ssl.server.cert.path": f"{self.ts.Variables.SSLDir}",
            "proxy.config.ssl.server.private_key.path": f"{self.ts.Variables.SSLDir}",
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "proxyprotocol",
        })

    def addTestCase0(self):
        """
        Incoming PROXY Protocol v1 on TCP port
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.httpbin)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Command = f"curl -vs --haproxy-protocol http://localhost:{self.ts.Variables.port}/get | {self.json_printer}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_0_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/test_case_0_stderr.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def addTestCase1(self):
        """
        Incoming PROXY Protocol v1 on SSL port
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.Command = f"curl -vsk --haproxy-protocol --http1.1 https://localhost:{self.ts.Variables.ssl_port}/get | {self.json_printer}"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/test_case_1_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/test_case_1_stderr.gold"
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def run(self):
        self.addTestCase0()
        self.addTestCase1()


ProxyProtocolTest().run()
