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

from enum import Enum
import os

Test.Summary = 'Exercise HTTP CONNECT Method'
Test.ContinueOnFail = True


class ConnectTest:

    class State(Enum):
        """
        State of process
        """
        INIT = 0
        RUNNING = 1

    def __init__(self):
        self.state = self.State.INIT
        self.__setupOriginServer()
        self.__setupTS()

    def __setupOriginServer(self):
        self.httpbin = Test.MakeHttpBinServer("httpbin")

    def __setupTS(self):
        self.ts = Test.MakeATSProcess("ts", select_ports=True)

        self.ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http',
                'proxy.config.http.server_ports': f"{self.ts.Variables.port}",
                'proxy.config.http.connect_ports': f"{self.httpbin.Variables.Port}",
            })

        self.ts.Disk.remap_config.AddLines([
            f"map http://foo.com/ http://127.0.0.1:{self.httpbin.Variables.Port}/",
        ])

        self.ts.Disk.logging_yaml.AddLines(
            '''
logging:
  formats:
    - name: common
      format: '%<chi> - %<caun> [%<cqtn>] "%<cqtx>" %<pssc> %<pscl>'
  logs:
    - filename: access
      format: common
'''.split("\n"))

    def __checkProcessBefore(self, tr):
        if self.state == self.State.RUNNING:
            tr.StillRunningBefore = self.httpbin
            tr.StillRunningBefore = self.ts
        else:
            tr.Processes.Default.StartBefore(self.httpbin, ready=When.PortOpen(self.httpbin.Variables.Port))
            tr.Processes.Default.StartBefore(self.ts)
            self.state = self.State.RUNNING

    def __checkProcessAfter(self, tr):
        assert (self.state == self.State.RUNNING)
        tr.StillRunningAfter = self.httpbin
        tr.StillRunningAfter = self.ts

    def __testCase0(self):
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"curl -v --fail -s -p -x 127.0.0.1:{self.ts.Variables.port} 'http://foo.com/get'"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = "gold/connect_0_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testAccessLog(self):
        """Wait for log file to appear, then wait one extra second to make sure TS is done writing it."""
        Test.Disk.File(os.path.join(self.ts.Variables.LOGDIR, 'access.log'), exists=True, content='gold/connect_access.gold')

        tr = Test.AddTestRun()
        tr.Processes.Default.Command = (
            os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
            os.path.join(self.ts.Variables.LOGDIR, 'access.log'))
        tr.Processes.Default.ReturnCode = 0

    def run(self):
        self.__testCase0()
        self.__testAccessLog()


ConnectTest().run()
