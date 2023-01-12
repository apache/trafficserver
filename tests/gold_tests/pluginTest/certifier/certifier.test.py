'''
Test cookie-related caching behaviors
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

Test.Summary = '''
Test certifier behaviors
'''
# **testname is required**
testName = ""

Test.SkipUnless(
    Condition.PluginExists('certifier.so')
)


class DynamicCertTest:
    chunkedReplayFile = "replays/https.replay.yaml"
    certPathSrc = os.path.join(Test.TestDirectory, "certs")
    # todo: might need to rename
    host = "www.tls.com"
    certPathDest = ""

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("verifier-server1", self.chunkedReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts2", enable_tls=True)
        self.ts.addDefaultSSLFiles()
        # copy over the cert store
        self.certPathDest = os.path.join(self.ts.Variables.CONFIGDIR, "certifier-certs")
        Setup.Copy(self.certPathSrc, self.certPathDest)
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http|certifier|ssl",
            "proxy.config.ssl.server.cert.path": f'{self.ts.Variables.SSLDir}',
            "proxy.config.ssl.server.private_key.path": f'{self.ts.Variables.SSLDir}',
        })
        self.ts.Disk.ssl_multicert_config.AddLine(
            'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
        )
        self.ts.Disk.remap_config.AddLine(
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/",
        )

        # todo: zli11 - os path join
        self.ts.Disk.plugin_config.AddLine(
            f"certifier.so -s {self.certPathDest}/store -m 1000 -c {self.certPathDest}/ca.cert -k {self.certPathDest}/ca.key -r {self.certPathDest}/ca-serial.txt")
        # Verify logs for dynamic generation of certs
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "Creating shadow certs",
            "Verify the certifier plugin generates the certificate dynamically.")

    def runHTTPSTraffic(self):
        tr = Test.AddTestRun("Test dynamic generation of certs")
        tr.AddVerifierClientProcess(
            "client1",
            self.chunkedReplayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def runHTTPSTraffic(self):
        tr = Test.AddTestRun("Test dynamic generation of certs")
        tr.AddVerifierClientProcess(
            "client1",
            self.chunkedReplayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def runHTTPSTraffic(self):
        tr = Test.AddTestRun("Test dynamic generation of certs")
        tr.AddVerifierClientProcess(
            "client1",
            self.chunkedReplayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        # the certifier plugin generates the cert and store it in a directory
        # named with the first three character of the md5 hash of the hostname
        genCertDirPath = os.path.join(self.certPathDest,
                                      'store',
                                      str(hashlib.md5(self.host.encode('utf-8')).hexdigest()[:3]))
        genCertPath = os.path.join(genCertDirPath, self.host + ".crt")
        genCertDir = Test.Disk.Directory(genCertDirPath)
        genCertFile = Test.Disk.File(genCertPath)
        genCertDir.Exists = False
        genCertFile.Exists = False
        print(f"the dir checking is {genCertDir}")
        self.runHTTPSTraffic()
        genCertDir.Exists = True
        genCertFile.Exists = True
        # todo: check content of cert with openssl


DynamicCertTest().run()
