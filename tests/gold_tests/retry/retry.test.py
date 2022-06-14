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

Test.Summary = '''
Test retries of request to the origin server
'''


class RetryTest:
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
        """
        Dead origin server. Do nothing on the port.
        """
        self.origin_server_port = Test.GetTcpPort("upstream_port")

    def __setupTS(self):
        self.ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True, enable_cache=False)

        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.origin_server_port}/")

        self.ts.Disk.records_config.update({
            "proxy.config.http.server_ports": f"{self.ts.Variables.port}",
            "proxy.config.http.connect_attempts_max_retries": 3,
            "proxy.config.diags.debug.enabled": 1,
            "proxy.config.diags.debug.tags": "http_hdrs",
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

    def __addTestCase0(self):
        """
        GET request
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"curl -s http://127.0.0.1:{self.ts.Variables.port}/"
        tr.Processes.Default.ReturnCode = 0
        self.__checkProcessAfter(tr)

    def __addTestCase1(self):
        """
        POST request with Transfer-Encoding: chunked
        """
        tr = Test.AddTestRun()
        self.__checkProcessBefore(tr)
        tr.Processes.Default.Command = f"curl -s http://127.0.0.1:{self.ts.Variables.port}/ -H 'Transfer-Encoding: chunked' -d 'hello: world'"
        tr.Processes.Default.ReturnCode = 0
        self.__checkProcessAfter(tr)

    def __testDebugLog(self):
        """
        Check debug log of ATS to compare proxy's retry requests.
        """
        self.ts.Streams.stderr.Content = "gold/debug.log.gold"

    def run(self):
        self.__addTestCase0()
        self.__addTestCase1()
        self.__testDebugLog()


RetryTest().run()
