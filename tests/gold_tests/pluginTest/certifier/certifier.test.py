'''
Test certifier plugin behaviors
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
Test certifier plugin behaviors
'''

Test.SkipUnless(Condition.PluginExists('certifier.so'))


class DynamicCertTest:
    httpsReplayFile = "replays/https.replay.yaml"
    certPathSrc = os.path.join(Test.TestDirectory, "certs")
    host = "www.tls.com"
    certPathDest = ""

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("verifier-server1", self.httpsReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts1", enable_tls=True)
        self.ts.addDefaultSSLFiles()
        # copy over the cert store in which the certs will be generated/stored
        self.certPathDest = os.path.join(self.ts.Variables.CONFIGDIR, "certifier-certs")
        Setup.Copy(self.certPathSrc, self.certPathDest)
        Setup.MakeDir(os.path.join(self.certPathDest, 'store'))
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|certifier|ssl",
                "proxy.config.ssl.server.cert.path": f'{self.ts.Variables.SSLDir}',
                "proxy.config.ssl.server.private_key.path": f'{self.ts.Variables.SSLDir}',
            })
        self.ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)
        self.ts.Disk.plugin_config.AddLine(
            f'certifier.so -s {os.path.join(self.certPathDest, "store")} -m 1000 -c {os.path.join(self.certPathDest, "ca.cert")} -k {os.path.join(self.certPathDest, "ca.key")} -r {os.path.join(self.certPathDest, "ca-serial.txt")}'
        )
        # Verify logs for dynamic generation of certs
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "creating shadow certs", "Verify the certifier plugin generates the certificate dynamically.")

    def runHTTPSTraffic(self):
        tr = Test.AddTestRun("Test dynamic generation of certs")
        tr.AddVerifierClientProcess(
            "client1",
            self.httpsReplayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def verifyCert(self, certPath):
        tr = Test.AddTestRun("Verify the content of the generated cert")
        tr.Processes.Default.Command = f'openssl x509 -in {certPath} -text -noout'
        tr.Processes.Default.ReturnCode = 0
        # Verify certificate content
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            "Subject: CN = www.tls.com", "Subject should match the host in the request")
        tr.Processes.Default.Streams.All += Testers.ContainsExpression(
            r"X509v3 extensions:\n.*X509v3 Subject Alternative Name:.*\n.*DNS:www.tls.com",
            "Should contain the SAN extension",
            reflags=re.MULTILINE)

    def verifyCertNotExist(self, certPath):
        tr = Test.AddTestRun("Verify the cert doesn't exist in the store")
        tr.Processes.Default.Command = "echo verify"
        certFile = tr.Disk.File(certPath, exists=False)

    def run(self):
        # the certifier plugin generates the cert and store it in a directory
        # named with the first three character of the md5 hash of the hostname
        genCertPath = os.path.join(
            self.certPathDest, 'store', str(hashlib.md5(self.host.encode('utf-8')).hexdigest()[:3]), self.host + ".crt")
        self.verifyCertNotExist(genCertPath)
        self.runHTTPSTraffic()
        self.verifyCert(genCertPath)


DynamicCertTest().run()


class ReuseExistingCertTest:
    httpsReplayFile = "replays/https-two-sessions.replay.yaml"
    certPathSrc = os.path.join(Test.TestDirectory, "certs")
    host = "www.tls.com"
    certPathDest = ""

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("verifier-server2", self.httpsReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts2", enable_tls=True)
        self.ts.addDefaultSSLFiles()
        # copy over the cert store in which the certs will be generated/stored
        self.certPathDest = os.path.join(self.ts.Variables.CONFIGDIR, "certifier-certs")
        Setup.Copy(self.certPathSrc, self.certPathDest)
        Setup.MakeDir(os.path.join(self.certPathDest, 'store'))
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|certifier|ssl",
                "proxy.config.ssl.server.cert.path": f'{self.ts.Variables.SSLDir}',
                "proxy.config.ssl.server.private_key.path": f'{self.ts.Variables.SSLDir}',
            })
        self.ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)
        self.ts.Disk.plugin_config.AddLine(
            f'certifier.so -s {os.path.join(self.certPathDest, "store")} -m 1000 -c {os.path.join(self.certPathDest, "ca.cert")} -k {os.path.join(self.certPathDest, "ca.key")} -r {os.path.join(self.certPathDest, "ca-serial.txt")}'
        )
        # Verify logs for reusing existing cert
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "reusing existing cert and context for www.tls.com", "Should reuse the existing certificate")

    def runHTTPSTraffic(self):
        tr = Test.AddTestRun("Test dynamic generation of certs")
        tr.AddVerifierClientProcess(
            "client2",
            self.httpsReplayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runHTTPSTraffic()


ReuseExistingCertTest().run()
