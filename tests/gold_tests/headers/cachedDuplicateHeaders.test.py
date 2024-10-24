'''
Test cached responses and requests with bodies
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
Test revalidating cached objects
'''
testName = "RevalidateCacheObject"
Test.ContinueOnFail = True


class CachedHeaderValidationTest:
    replay_file = "replays/cache-test.replay.yaml"

    def __init__(self):
        self.setupOriginServer()
        self.setupTS()

    def setupOriginServer(self):
        self.server = Test.MakeVerifierServerProcess("cached-header-verifier-server", self.replay_file)

    def setupTS(self):
        self.ts = Test.MakeATSProcess("ts", enable_tls=True)
        self.ts.Disk.plugin_config.AddLine('xdebug.so --enable=x-cache,x-cache-key,via')
        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.response_via_str': 3,
            })
        self.ts.Disk.remap_config.AddLine('map / http://127.0.0.1:{0}'.format(self.server.Variables.http_port))

    def runTraffic(self):
        tr = Test.AddTestRun()
        tr.AddVerifierClientProcess("cached-header-verifier-client", self.replay_file, http_ports=[self.ts.Variables.port])
        tr.Processes.Default.StartBefore(self.server)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.ts
        tr.StillRunningAfter = self.server

    def run(self):
        self.runTraffic()


CachedHeaderValidationTest().run()
