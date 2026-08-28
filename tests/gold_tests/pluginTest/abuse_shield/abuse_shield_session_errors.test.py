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
"""Verify session errors are tracked even without HTTP transactions."""

import sys

Test.Summary = "abuse_shield accounts for HTTP/2 session errors once per connection"
Test.SkipUnless(Condition.PluginExists('abuse_shield.so'))


class SessionErrorTest:

    def __init__(self, mode: str, trusted: bool = False) -> None:
        self._mode = mode
        self._trusted = trusted
        self._name = f"h2_{mode}_{int(trusted)}"
        self._setup_ts()
        self._send_errors()
        self._check_metrics()
        if mode == "block":
            self._check_block()

    def _setup_ts(self) -> None:
        self._ts = Test.MakeATSProcess(self._name, enable_tls=True)
        self._ts.addDefaultSSLFiles()
        if self._mode == "limit":
            self._origin = Test.MakeOriginServer("h2_limit_origin")
            self._origin.addResponse(
                "session.json", {
                    "headers": "POST / HTTP/1.1\r\nHost: localhost\r\n\r\n",
                    "timestamp": "1",
                    "body": ""
                }, {
                    "headers": "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n",
                    "timestamp": "1",
                    "body": "OK"
                })
            self._ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._origin.Variables.Port}/")
        self._ts.Disk.ssl_multicert_yaml.AddLines(
            ["ssl_multicert:", "- dest_ip: '*'", "  ssl_cert_name: server.pem", "  ssl_key_name: server.key"])
        self._ts.Disk.records_config.update(
            {
                'proxy.config.http2.max_concurrent_streams_in': 2,
                'proxy.config.ssl.server.cert.path': self._ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': self._ts.Variables.SSLDir,
            })
        trusted_config = ""
        if self._trusted:
            path = self._ts.Variables.CONFIGDIR + "/trusted.yaml"
            self._ts.Disk.File(path, typename="ats:config").AddLines(["trusted_ips:", "  - 127.0.0.1"])
            trusted_config = f"  trusted_ips_file: {path}"
        actions = "log, block, close" if self._mode == "block" else "log"
        self._ts.Disk.File(
            self._ts.Variables.CONFIGDIR + "/abuse_shield.yaml", typename="ats:config").AddLines(
                f"""
global:
  ip_tracking:
    slots: 1000
  log_interval_sec: 0
{trusted_config}
rules:
  - name: session_errors
    filter:
      max_h2_error_rate: 1
    action: [{actions}]
enabled: true
""".strip().splitlines())
        self._ts.Disk.plugin_config.AddLine('abuse_shield.so abuse_shield.yaml')
        self._ts.Disk.diags_log.Content = Testers.ContainsExpression(
            "Plugin initialized", "The plugin loads; malformed frames intentionally produce core error diagnostics.")
        if self._mode == "limit":
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "beyond max_concurrent limit", "The client reaches the stream-refusal path.")

    def _send_errors(self) -> None:
        tr = Test.AddTestRun(f"Send {self._name} traffic")
        tr.Processes.Default.Command = (
            f"{sys.executable} {Test.TestDirectory}/h2_session_errors.py "
            f"--port {self._ts.Variables.ssl_port} --mode {self._mode}")
        tr.Processes.Default.ReturnCode = 0
        if self._mode == "limit":
            tr.Processes.Default.StartBefore(self._origin)
            tr.StillRunningAfter = self._origin
        tr.Processes.Default.StartBefore(self._ts)
        tr.StillRunningAfter = self._ts

    def _check_metrics(self) -> None:
        expected = 0 if self._trusted or self._mode == "normal" else 8
        tr = Test.AddTestRun(f"Check {self._name} error accounting")
        tr.Processes.Default.Command = (
            "traffic_ctl metric get abuse_shield.h2.events proxy.process.http2.total_client_streams abuse_shield.actions.logged")
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            rf"abuse_shield.h2.events\s+{expected}\b", "Each session error is counted exactly once.")
        streams = 16 if self._mode == "limit" else 0
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            rf"proxy.process.http2.total_client_streams\s+{streams}\b",
            "Only the admitted streams exist; session errors do not need a transaction.")
        log_count = "0" if expected == 0 else "[1-9][0-9]*"
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            rf"abuse_shield.actions.logged\s+{log_count}\b", "Logging follows the error policy and trusted bypass.")
        tr.StillRunningAfter = self._ts
        if self._mode == "limit":
            tr.StillRunningAfter = self._origin

    def _check_block(self) -> None:
        tr = Test.AddTestRun("Reject another connection after session errors exceed the rate")
        tr.Processes.Default.Command = (f"curl -k -s --max-time 2 https://127.0.0.1:{self._ts.Variables.ssl_port}/")
        tr.Processes.Default.ReturnCode = Any(28, 35, 52, 55, 56)
        tr.StillRunningAfter = self._ts
        tr = Test.AddTestRun("Verify session-level blocking metrics")
        tr.Processes.Default.Command = (
            "traffic_ctl metric get abuse_shield.actions.blocked abuse_shield.connections.rejected "
            "abuse_shield.actions.closed abuse_shield.actions.close_failed")
        tr.Processes.Default.Env = self._ts.Env
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            r"abuse_shield.actions.blocked\s+[1-9][0-9]*", "Session errors triggered a block.")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r"abuse_shield.connections.rejected\s+1\b", "The next connection was rejected.")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r"abuse_shield.actions.closed\s+0\b", "Closing an already closed session is not counted as an action.")
        tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
            r"abuse_shield.actions.close_failed\s+0\b", "An already closed session does not attempt another close.")
        tr.StillRunningAfter = self._ts


SessionErrorTest("invalid")
SessionErrorTest("limit")
SessionErrorTest("received")
SessionErrorTest("normal")
SessionErrorTest("invalid", trusted=True)
SessionErrorTest("block")
