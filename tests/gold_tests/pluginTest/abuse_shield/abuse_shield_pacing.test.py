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
"""Verify compliant pacing, periodic gauges, and clearing tracking state."""
import sys
import re

Test.Summary = "Compliant 30 requests/second must never trigger enforcement."
Test.SkipUnless(Condition.PluginExists("abuse_shield.so"))


class PacingTest:

    def __init__(self) -> None:
        self.ts = Test.MakeATSProcess("pace", enable_cache=True)
        self.origin = Test.MakeOriginServer("origin")
        self.origin.addResponse(
            "sessionlog.json", {
                "headers": "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": ""
            }, {
                "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\nCache-Control: max-age=300\r\n\r\n",
                "timestamp": "1469733493.993",
                "body": "OK"
            })
        self.ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self.origin.Variables.Port}")
        self.ts.Disk.plugin_config.AddLine("abuse_shield.so pace.yaml")
        config = self.ts.Disk.File(f"{self.ts.Variables.CONFIGDIR}/pace.yaml", typename="ats:config")
        # Only the request half of the first rule is exceeded: AND must not match.
        config.AddLines(
            [
                "global: {ip_tracking: {slots: 10}, log_file: abuse_shield}", "rules:",
                "  - {name: combined, filter: {max_req_rate: 1, max_conn_rate: 5}, action: [block, close]}",
                "  - {name: paced, filter: {max_req_rate: 30}, action: [log, block, close]}"
            ])
        tr = Test.AddTestRun("Send compliant traffic")
        tr.Processes.Default.Command = (f"{sys.executable} {Test.TestDirectory}/paced_requests.py --port {self.ts.Variables.port}")
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(self.origin)
        tr.Processes.Default.StartBefore(self.ts)
        tr.StillRunningAfter = self.origin
        tr.StillRunningAfter = self.ts
        self.check_metrics("No false matches; gauge updates without a stats message", 300, 1)
        tr = Test.AddTestRun("Clear tracking state")
        tr.Processes.Default.Command = "traffic_ctl plugin msg abuse_shield.clear"
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.origin
        tr.StillRunningAfter = self.ts
        tr = Test.AddTestRun("Wait for clear to complete")
        waiter = tr.Processes.Process("await_clear", "sleep 30")
        waiter.Ready = When.FileContains(self.ts.Disk.diags_log.Name, "Tracking and block state cleared")
        tr.Processes.Default.StartBefore(waiter)
        tr.Processes.Default.Command = "true"
        tr.Processes.Default.ReturnCode = 0
        tr.StillRunningAfter = self.origin
        tr.StillRunningAfter = self.ts
        self.check_metrics("Clear releases slots and preserves counters", 300, 0)

    def check_metrics(self, summary: str, events: int, slots: int) -> None:
        tr = Test.AddTestRun(summary)
        metrics = {
            "txn.events": events,
            "txn.slots_used": slots,
            "rules.matched": 0,
            "actions.blocked": 0,
            "actions.closed": 0,
            "connections.rejected": 0
        }
        tr.Processes.Default.Command = "traffic_ctl metric get " + " ".join(f"abuse_shield.{name}" for name in metrics)
        tr.Processes.Default.Env = self.ts.Env
        tr.Processes.Default.ReturnCode = 0
        for name, expected in metrics.items():
            tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
                rf"abuse_shield\.{re.escape(name)} {expected}\b", f"{name} equals {expected}")
        tr.StillRunningAfter = self.origin
        tr.StillRunningAfter = self.ts


PacingTest()
