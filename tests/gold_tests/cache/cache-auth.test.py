'''
Test authorization-related caching behaviors
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

Test.Summary = '''
Test authorization-related caching behaviors
'''

Test.ContinueOnFail = True

# **testname is required**
testName = ""


class AuthDefaultTest:
    # Verify the proper caching behavior for request/response containing
    # auth-related fields when ATS is in default configuration
    authDefaultReplayFile = "replay/auth-default.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("auth-default-verifier-server", self.authDefaultReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-auth-default")
        self.ts.Disk.records_config.update({
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http",
        })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

        # Verify log for skipping the WWW-Authenticate response
        self.ts.Disk.traffic_out.Content += Testers.ContainsExpression(
            "response has WWW-Authenticate, response is not cacheable",
            "Verify ATS doesn't store the response with WWW-Authenticate.")

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the proper caching behavior for request/response containing auth-related fields when ATS is in default configuration"
        )
        tr.AddVerifierClientProcess(
            "auth-default-client", self.authDefaultReplayFile, http_ports=[self.ts.Variables.port], other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


AuthDefaultTest().run()


class AuthIgnoredTest:
    # Verify the proper caching behavior for request/response containing
    # auth-related fields when ATS is configured to bypass caching for those
    authIgnoredReplayFile = "replay/auth-ignored.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("auth-ignored-verifier-server", self.authIgnoredReplayFile)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts-auth-ignored")
        self.ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http",
                # Configure ATS to ignore the WWW-Authenticate header in
                # response(allow caching of such response)
                "proxy.config.http.cache.ignore_authentication": 1
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.server.Variables.http_port}/",)

    def runTraffic(self):
        tr = Test.AddTestRun(
            "Verify the proper caching behavior for request/response containing auth-related fields when ATS is configured to bypass caching for those"
        )
        tr.AddVerifierClientProcess(
            "auth-ignored-client", self.authIgnoredReplayFile, http_ports=[self.ts.Variables.port], other_args='--thread-limit 1')
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.server
        tr.StillRunningAfter = self.ts

    def run(self):
        self.runTraffic()


AuthIgnoredTest().run()
