'''
Verify ATS retries non-idempotent outbound HTTP/2 requests when the origin
asserts that it did not process them, and rejects retries without a complete
body copy or when response evidence contradicts that assertion.
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
import sys

from ports import get_port

Test.Summary = '''
Verify POST retry admission and rejection for resets, GOAWAY boundaries,
body availability, response evidence, and invalid origin frames.
'''

Test.ContinueOnFail = True


class SafeRetryScenario:
    """Configure one HTTP/2 safe-retry scenario."""

    next_id = 0

    replay_file = "replay_h2o_safe_retry/safe_retry.replay.yaml"

    def __init__(self, mode: str, replay_key: str, *, buffering: int = 1, copy_size: int = 4096, retry: bool = True) -> None:
        name = f"{mode}-{buffering}-{copy_size}"
        tr = Test.AddTestRun(f"Safe POST retry after outbound HTTP/2 {mode}")
        tr.Setup.Copy("safe_retry_origin.py")
        tr.Setup.Copy("origin_lifecycle.py")
        stop_file = f"{tr.RunDirectory}/stop-{name}"

        server = Test.Processes.Process(f"safe-retry-origin-{name}")
        server_port = get_port(server, "https_port")
        server_pem = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
        server_key = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")
        server.Setup.Copy(server_pem)
        server.Setup.Copy(server_key)
        server.Command = (
            f"{sys.executable} {tr.RunDirectory}/safe_retry_origin.py "
            f"{mode} {server_port} server.pem server.key {stop_file} {2 if retry else 1}")
        server.Ready = When.PortOpen(server_port)
        server.ReturnCode = 0

        server.Streams.stdout += Testers.ContainsExpression(
            "request_received attempt=1", "The origin must receive the first request.")
        if retry:
            server.Streams.stdout += Testers.ContainsExpression(
                r"retry_succeeded attempts=2 method=POST body=request-body",
                "ATS must retry the POST once, including its request body.")
        else:
            server.Streams.stdout += Testers.ExcludesExpression("request_received attempt=2", "ATS must not replay this POST.")

        if mode in ("rst", "early-rst", "rst-internal", "rst-cancel", "response-rst"):
            error = {"rst-internal": "INTERNAL_ERROR", "rst-cancel": "CANCEL"}.get(mode, "REFUSED_STREAM")
            server.Streams.stdout += Testers.ContainsExpression(
                f"action=RST_STREAM attempt=1 error={error}", "The origin must send the intended reset.")
        elif mode.startswith("goaway"):
            last_id = "[1-9][0-9]*" if mode == "goaway-equal" else "0"
            server.Streams.stdout += Testers.ContainsExpression(
                f"action=GOAWAY attempt=1 last_stream_id={last_id}", "The origin must use the intended GOAWAY boundary.")

        if mode in ("unsolicited-continuation", "settings-flood"):
            code = 1 if mode == "unsolicited-continuation" else 11
            server.Streams.stdout += Testers.ContainsExpression(
                f"peer_goaway error={code}", "ATS must reject invalid origin frames with the expected error.")

        # Keep runroot paths below the Unix-domain socket length limit.
        ts = Test.MakeATSProcess(f"ts{SafeRetryScenario.next_id}", enable_tls=True, enable_cache=False)
        SafeRetryScenario.next_id += 1
        if mode in ("unsolicited-continuation", "settings-flood"):
            code = "01" if mode == "unsolicited-continuation" else "0b"
            reason = "unsolicited CONTINUATION frame" if code == "01" else "recv settings too frequent SETTINGS frames"
            ts.Disk.diags_log.Content = Testers.ExcludesExpression(
                f"ERROR:(?! HTTP/2 connection error code=0x{code}.*{reason})",
                "Only the deliberately triggered protocol error is allowed.")
            ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                "FATAL:|Unrecognized configuration value", "No fatal/config errors.")
        ts.addDefaultSSLFiles()
        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
                'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.http.server_session_sharing.pool': 'thread',
                # The RST_STREAM retry may reuse the healthy H2 connection.
                # Match only on the loopback IP because an IP-literal remap
                # does not send SNI, while ATS proposes the literal as the
                # lookup SNI when it searches the session pool.
                'proxy.config.http.server_session_sharing.match': 'ip',
                'proxy.config.http.connect_attempts_max_retries': 3,
                # Retrying a request with a body requires ATS's existing
                # request buffer so the second origin attempt can replay it.
                'proxy.config.http.request_buffer_enabled': buffering,
                'proxy.config.http.post_copy_size': copy_size,
                'proxy.config.http.number_of_redirections': 1 if copy_size == 4 else 0,
                'proxy.config.http2.max_settings_frames_per_minute': 2,
                'proxy.config.exec_thread.autoconfig.enabled': 0,
                'proxy.config.exec_thread.limit': 1,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|http2',
            })
        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server_port}')
        ts.Disk.ssl_multicert_yaml.AddLines(
            ['ssl_multicert:', '  - dest_ip: \"*\"', '    ssl_cert_name: server.pem', '    ssl_key_name: server.key'])

        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
        if retry:
            tr.AddVerifierClientProcess(
                f"safe-retry-client-{name}", self.replay_file, http_ports=[ts.Variables.port], keys=replay_key)
        else:
            tr.Setup.Copy("negative_retry_client.py")
            tr.Processes.Default.Command = f"{sys.executable} {tr.RunDirectory}/negative_retry_client.py {ts.Variables.port}"
            tr.Processes.Default.ReturnCode = 0
        tr.TimeOut = 60
        tr.StillRunningAfter = ts
        tr.StillRunningAfter = server

        # Admission counters observe the decision even if an unwanted retry opens
        # a fresh connection that this serial test origin cannot accept yet.
        body_denied = not retry and mode in ("rst", "goaway", "early-rst")
        outcome = "admitted" if retry else "body-denied" if body_denied else "not-admitted"
        metrics = Test.AddTestRun(f"Verify retry decision counters for {name}")
        metrics.Processes.Default.Command = (
            f"{Test.Variables.AtsTestToolsDir}/stdout_wait 10"
            f" 'traffic_ctl metric get proxy.process.http.origin.retry_admitted"
            f" proxy.process.http.origin.retry_body_unavailable'"
            f" {Test.TestDirectory}/gold/h2o-retry-{outcome}.gold")
        metrics.Processes.Default.Env = ts.Env
        metrics.Processes.Default.ReturnCode = 0
        metrics.TimeOut = 15
        metrics.StillRunningAfter = server

        shutdown = Test.AddTestRun(f"Stop origin after validating {name}")
        shutdown.Processes.Default.Command = f"{sys.executable} {tr.RunDirectory}/origin_lifecycle.py {stop_file}"
        shutdown.Processes.Default.ReturnCode = 0
        shutdown.TimeOut = 15

        if retry:
            ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
                "ERR_CLIENT_ABORT", "A replayable, unprocessed request must not fail as a client abort.")


SafeRetryScenario("rst", "rst-refused")
SafeRetryScenario("goaway", "goaway-last-stream-zero")

SafeRetryScenario("goaway-reserved", "goaway-last-stream-zero")
for mode in ("rst", "goaway"):
    SafeRetryScenario(mode, "no-retry", buffering=0, retry=False)
    SafeRetryScenario(mode, "no-retry", buffering=0, copy_size=4, retry=False)
for mode in ("rst-internal", "rst-cancel", "goaway-equal", "response-rst"):
    SafeRetryScenario(mode, "no-retry", retry=False)

for mode in ("unsolicited-continuation", "settings-flood"):
    SafeRetryScenario(mode, "no-retry", retry=False)

SafeRetryScenario("early-rst", "rst-refused")
SafeRetryScenario("early-rst", "no-retry", buffering=0, retry=False)
