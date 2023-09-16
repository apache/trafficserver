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

import os

Test.Summary = '''
Test invalid values in range header
'''
Test.ContinueOnFail = True


class InvalidRangeHeaderTest:
    invalidRangeRequestReplayFile = "replays/invalid_range_request.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("verifier-server1", self.invalidRangeRequestReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts1")
        self.ts.Disk.records_config.update({'proxy.config.diags.debug.enabled': 1,
                                            'proxy.config.diags.debug.tags': 'http',
                                            'proxy.config.http.cache.http': 1,
                                            'proxy.config.http.cache.range.write': 1,
                                            'proxy.config.http.cache.required_headers': 0,
                                            'proxy.config.http.insert_age_in_response': 0})
        self.ts.Disk.remap_config.AddLine(
            f"map / http://127.0.0.1:{self.server.Variables.http_port}/",
        )

    def runTraffic(self):
        tr = Test.AddTestRun()
        tr.AddVerifierClientProcess(
            "client1",
            self.invalidRangeRequestReplayFile,
            http_ports=[self.ts.Variables.port],
            other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

        # verification
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r"Received an HTTP/1 416 response for key 2",
            "Verify that client receives a 416 response")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r"x-responseheader: failed_response",
            "Verify that the response came from the server")

    def run(self):
        self.runTraffic()


InvalidRangeHeaderTest().run()
