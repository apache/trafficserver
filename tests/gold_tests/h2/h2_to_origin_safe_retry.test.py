'''
Verify ATS retries non-idempotent outbound HTTP/2 requests when the origin
guarantees that it did not process them.
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
Verify that ATS safely retries a POST after the HTTP/2 origin either sends
RST_STREAM(REFUSED_STREAM) or GOAWAY(last_stream_id=0).
'''

Test.ContinueOnFail = True


class SafeRetryScenario:
    """Configure one HTTP/2 safe-retry scenario."""

    replay_file = "replay_h2o_safe_retry/safe_retry.replay.yaml"

    def __init__(self, mode: str, replay_key: str) -> None:
        tr = Test.AddTestRun(f"Safe POST retry after outbound HTTP/2 {mode}")
        tr.Setup.Copy("safe_retry_origin.py")

        server = tr.Processes.Process(f"safe-retry-origin-{mode}")
        server_port = get_port(server, "https_port")
        server_pem = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.pem")
        server_key = os.path.join(Test.Variables.AtsTestToolsDir, "ssl", "server.key")
        server.Setup.Copy(server_pem)
        server.Setup.Copy(server_key)
        server.Command = (
            f"{sys.executable} {tr.RunDirectory}/safe_retry_origin.py "
            f"{mode} {server_port} server.pem server.key")
        server.Ready = When.PortOpen(server_port)
        server.ReturnCode = Any(0, -2)

        action = "REFUSED_STREAM" if mode == "rst" else "GOAWAY"
        server.Streams.stdout += Testers.ContainsExpression(
            rf"action={action} attempt=1",
            f"The origin must reject the first POST with {action}.",
        )
        server.Streams.stdout += Testers.ContainsExpression(
            r"retry_succeeded attempts=2 method=POST body=request-body",
            "ATS must retry the POST once, including its request body.",
        )

        ts = tr.MakeATSProcess(f"ts-safe-retry-{mode}", enable_tls=True, enable_cache=False)
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
                'proxy.config.http.request_buffer_enabled': 1,
                'proxy.config.http.post_copy_size': 4096,
                'proxy.config.exec_thread.autoconfig.enabled': 0,
                'proxy.config.exec_thread.limit': 1,
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|http2',
            })
        ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server_port}')
        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
        tr.AddVerifierClientProcess(
            f"safe-retry-client-{mode}",
            self.replay_file,
            http_ports=[ts.Variables.port],
            keys=replay_key,
        )
        tr.TimeOut = 60

        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            "ERR_CLIENT_ABORT",
            "A request guaranteed unprocessed by the origin must not be returned as a client abort.",
        )


SafeRetryScenario("rst", "rst-refused")
SafeRetryScenario("goaway", "goaway-last-stream-zero")
