'''
Test ATS configured to not respond with chunked content.
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

Test.Summary = 'Test ATS configured to not respond with chunked content.'


class ChunkedEncoding:
    chunkedReplayFile = "replays/chunked_2.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("server", self.chunkedReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_tls=True, enable_cache=False)
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",

            # Respond with chunked encoding.
            "proxy.config.http.chunking_enabled": 1,
        })
        self.ts.Disk.remap_config.AddLines([
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/",
        ])

    def runChunkedTraffic(self):
        tr = Test.AddTestRun()
        tr.AddVerifierClientProcess(
            "client",
            self.chunkedReplayFile,
            http_ports=[self.ts.Variables.port],
            https_ports=[self.ts.Variables.ssl_port])
        tr.Processes.Default.Streams.stdout += "gold/verifier_client_chunked_2.gold"

        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runChunkedTraffic()


ChunkedEncoding().run()
