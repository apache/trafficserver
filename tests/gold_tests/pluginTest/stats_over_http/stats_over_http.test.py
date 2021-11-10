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

Test.Summary = 'Exercise stats-over-http plugin'
Test.SkipUnless(Condition.PluginExists('stats_over_http.so'))
Test.ContinueOnFail = True


class StatsOverHttpPluginTest:
    """
    https://docs.trafficserver.apache.org/en/latest/admin-guide/plugins/stats_over_http.en.html
    """

    class State(Enum):
        """
        State of process
        """
        INIT = 0
        RUNNING = 1

    def __init__(self):
        self.state = self.State.INIT
        self.__setupTS()

    def __setupTS(self):
        self.ts = Test.MakeATSProcess("ts", select_ports=True)

        self.ts.Disk.plugin_config.AddLine('stats_over_http.so _stats')

        self.ts.Disk.records_config.update({
            "proxy.config.http.server_ports": f"{self.ts.Variables.port}",
        })

    def __checkProcessBefore(self, tr):
        if self.state == self.State.RUNNING:
            tr.StillRunningBefore = self.ts
        else:
            tr.Processes.Default.StartBefore(self.ts)
            self.state = self.State.RUNNING

    def __checkProcessAfter(self, tr):
        assert (self.state == self.State.RUNNING)
        tr.StillRunningAfter = self.ts

    def __testCase0(self):
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"curl -vs --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats"
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = "gold/stats_over_http_0_stdout.gold"
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_0_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def run(self):
        self.__testCase0()


StatsOverHttpPluginTest().run()
