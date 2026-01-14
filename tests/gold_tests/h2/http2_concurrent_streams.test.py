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

Test.Sumary = '''
Verify Concurrent Streams Handling
'''


class Http2ConcurrentStreamsTest:
    replayFile = "replay/http2_concurrent_streams.replay.yaml"

    def __init__(self):
        self.__setupOriginServer()
        self.__setupTS()

    def __setupOriginServer(self):
        self._server = Test.MakeVerifierServerProcess("verifier-server", self.replayFile)

    def __setupTS(self):
        self._ts = Test.MakeATSProcess(f"ts", enable_tls=True, enable_cache=True)
        self._ts.addDefaultSSLFiles()
        self._ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http2',
                'proxy.config.ssl.server.cert.path': f"{self._ts.Variables.SSLDir}",
                'proxy.config.ssl.server.private_key.path': f"{self._ts.Variables.SSLDir}",
                'proxy.config.http.insert_response_via_str': 2,
            })
        self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.http_port}")
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

    def run(self):
        tr = Test.AddTestRun()
        tr.AddVerifierClientProcess(
            "verifier-client", self.replayFile, http_ports=[self._ts.Variables.port], https_ports=[self._ts.Variables.ssl_port])
        tr.Processes.Default.StartBefore(self._ts)
        tr.Processes.Default.StartBefore(self._server)
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server


Http2ConcurrentStreamsTest().run()
