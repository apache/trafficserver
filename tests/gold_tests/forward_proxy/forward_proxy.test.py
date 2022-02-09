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

Test.Summary = 'Verify ATS can function as a forward proxy'
Test.ContinueOnFail = True


class ForwardProxyTest:
    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("server", "forward_proxy.replay.yaml")
        self.server.Streams.All = Testers.ContainsExpression(
            'Received an HTTP/1 request with key 1',
            'Verify that the server received the request.')

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")
        self.ts.Disk.remap_config.AddLine(
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/")

        self.ts.Disk.records_config.update({
            'proxy.config.ssl.server.cert.path': self.ts.Variables.SSLDir,
            'proxy.config.ssl.server.private_key.path': self.ts.Variables.SSLDir,
            'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            'proxy.config.ssl.keylog_file': '/tmp/keylog.txt',

            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': "http",
        })

    def addProxyHttpsToHttpCase(self):
        """
        Test ATS as an HTTPS forward proxy behind an HTTP server.
        """
        tr = Test.AddTestRun()
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.Command = (
            f'curl --proxy-insecure -v -H "uuid: 1" '
            f'--proxy "https://127.0.0.1:{self.ts.Variables.ssl_port}/" '
            f'http://example.com/')
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

        tr.Processes.Default.Streams.All = Testers.ContainsExpression(
            '< HTTP/1.1 200 OK',
            'Verify that curl received a 200 OK response.')

    def run(self):
        self.addProxyHttpsToHttpCase()


ForwardProxyTest().run()
