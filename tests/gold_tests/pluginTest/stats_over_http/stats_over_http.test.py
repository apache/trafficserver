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
import sys

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
        self.ts = Test.MakeATSProcess("ts")

        self.ts.Disk.plugin_config.AddLine('stats_over_http.so _stats')

        self.ts.Disk.records_config.update(
            {
                "proxy.config.http.server_ports": f"{self.ts.Variables.port}",
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "stats_over_http"
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

    def __testCaseNoAccept(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in JSON format: no Accept and default path')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('{ "global": {', 'Output should have the JSON header.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            '"proxy.process.http.delete_requests": "0",', 'Output should be JSON formatted.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_json_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testCaseAcceptCSV(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in CSV format')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs -H'Accept: text/csv' --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'proxy.process.http.delete_requests,0', 'Output should be CSV formatted.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_csv_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testCaseAcceptPrometheus(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in Prometheus format')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs -H'Accept: text/plain; version=0.0.4' --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'proxy_process_http_delete_requests 0', 'Output should be Prometheus formatted.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_prometheus_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testCasePathJSON(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in JSON format via /_stats/json')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats/json")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression('{ "global": {', 'JSON header expected.')
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            '"proxy.process.http.delete_requests": "0",', 'JSON field expected.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_json_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testCasePathCSV(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in CSV format via /_stats/csv')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats/csv")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'proxy.process.http.delete_requests,0', 'CSV output expected.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_csv_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testCasePathPrometheus(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in Prometheus format via /_stats/prometheus')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats/prometheus")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'proxy_process_http_delete_requests 0', 'Prometheus output expected.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_prometheus_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __testCaseAcceptIgnoredIfPathExplicit(self):
        tr = Test.AddTestRun('Fetch stats over HTTP in Prometheus format with Accept csv header')
        self.__checkProcessBefore(tr)
        tr.MakeCurlCommand(f"-vs -H'Accept: text/csv' --http1.1 http://127.0.0.1:{self.ts.Variables.port}/_stats/prometheus")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            'proxy_process_http_delete_requests 0', 'Prometheus output expected.')
        tr.Processes.Default.Streams.stderr = "gold/stats_over_http_prometheus_stderr.gold"
        tr.Processes.Default.TimeOut = 3
        self.__checkProcessAfter(tr)

    def __queryAndParsePrometheusMetrics(self):
        """
        Query the ATS stats over HTTP in Prometheus format and parse the output.
        """
        tr = Test.AddTestRun('Query and parse Prometheus metrics')
        ingester = 'prometheus_stats_ingester.py'
        tr.Setup.CopyAs(ingester)
        self.__checkProcessBefore(tr)
        p = tr.Processes.Default
        p.Command = f'{sys.executable} {ingester} http://127.0.0.1:{self.ts.Variables.port}/_stats/prometheus'
        p.ReturnCode = 0
        p.Streams.stdout += Testers.ContainsExpression(
            'proxy_process_http_delete_requests 0', 'Verify the successful parsing of Prometheus metrics.')

    def run(self):
        self.__testCaseNoAccept()
        self.__testCaseAcceptCSV()
        self.__testCaseAcceptPrometheus()
        self.__testCasePathJSON()
        self.__testCasePathCSV()
        self.__testCasePathPrometheus()
        self.__testCaseAcceptIgnoredIfPathExplicit()
        self.__queryAndParsePrometheusMetrics()


StatsOverHttpPluginTest().run()
