'''
Test compress plugin with range request
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

Test.Summary = '''
Test compress plugin with range request
'''

Test.SkipUnless(Condition.PluginExists('compress.so'))


class CompressPluginTest:
    replayFile = "replay/compress-and-range.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("verifier-server", self.replayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|compress",
                "proxy.config.http.insert_response_via_str": 2,
            })

        self.ts.Setup.Copy("etc/cache-true-remove-range.config")
        self.ts.Setup.Copy("etc/cache-true-remove-accept-encoding.config")
        self.ts.Setup.Copy("etc/cache-true-no-compression.config")

        self.ts.Disk.remap_config.AddLines(
            {
                f"""
map /cache-true-remove-range/ http://127.0.0.1:{self.server.Variables.http_port}/ \
    @plugin=compress.so \
    @pparam={Test.RunDirectory}/cache-true-remove-range.config
map /cache-true-remove-accept-encoding/ http://127.0.0.1:{self.server.Variables.http_port}/ \
    @plugin=compress.so \
    @pparam={Test.RunDirectory}/cache-true-remove-accept-encoding.config
map /cache-true-no-compression/ http://127.0.0.1:{self.server.Variables.http_port}/ \
    @plugin=compress.so \
    @pparam={Test.RunDirectory}/cache-true-no-compression.config
"""
            })

    def run(self):
        tr = Test.AddTestRun()
        tr.AddVerifierClientProcess(
            "verifier-client", self.replayFile, http_ports=[self.ts.Variables.port], other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.ts)
        tr.Processes.Default.StartBefore(self.server)
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server


CompressPluginTest().run()
