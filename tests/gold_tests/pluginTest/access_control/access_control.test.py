'''
Test access control plugin behaviors
'''
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the #  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
import hashlib
import os
import re

Test.Summary = '''
Test access control plugin behaviors
'''

Test.SkipUnless(Condition.PluginExists('access_control.so'))


class AccessControlTest:
    replayFile = "replays/access_control.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("verifier-server", self.replayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_tls=True)
        self.ts.addDefaultSSLFiles()
        self.ts.Disk.ssl_multicert_config.AddLine("dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|access_control",
                "proxy.config.http.insert_response_via_str": 2,
                'proxy.config.ssl.server.cert.path': f"{self.ts.Variables.SSLDir}",
                'proxy.config.ssl.server.private_key.path': f"{self.ts.Variables.SSLDir}",
                'proxy.config.ssl.client.alpn_protocols': 'http/1.1',
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
            })

        self.ts.Setup.Copy("etc/hmac_keys.txt")
        self.ts.Disk.remap_config.AddLines(
            {
                f'''map / https://127.0.0.1:{self.server.Variables.https_port}/ \
    @plugin=access_control.so \
    @pparam=--symmetric-keys-map={Test.RunDirectory}/hmac_keys.txt \
    @pparam=--check-cookie=TokenCookie \
    @pparam=--extract-subject-to-header=@TokenSubject \
    @pparam=--extract-tokenid-to-header=@TokenId \
    @pparam=--extract-status-to-header=@TokenStatus \
    @pparam=--token-response-header=TokenRespHdr'''
            })

    def run(self):
        tr = Test.AddTestRun("Session Cookie")
        tr.AddVerifierClientProcess(
            "verifier-client",
            self.replayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.StartBefore(self.server)
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server


AccessControlTest().run()
