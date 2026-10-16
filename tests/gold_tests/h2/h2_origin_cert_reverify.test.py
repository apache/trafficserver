'''
Verify HTTP/2 origin session reuse re-checks the origin certificate name.
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
Verify HTTP/2 origin session reuse re-checks the origin certificate name.
'''


class TestOriginCertReverify:
    """Verify reuse mismatches are logged according to the verification policy."""

    def __init__(self, policy: str) -> None:
        self._configure_processes(policy)
        self._configure_requests(policy)
        self._configure_logging_checks(policy)

    def _configure_processes(self, policy: str) -> None:
        ts = self._ts = Test.MakeATSProcess(f"ts-{policy.lower()}", enable_tls=True, enable_cache=False)
        server = self._server = Test.MakeVerifierServerProcess(
            f"h2-origin-{policy.lower()}",
            "replay_h2_origin_cert_reverify.yaml",
            ssl_cert="../tls/ssl/signed-foo.pem",
            ca_cert="../tls/ssl/signer.pem")

        ts.addDefaultSSLFiles()
        ts.addSSLfile("../tls/ssl/signer.pem")

        ts.Disk.records_config.update(
            {
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.CA.cert.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.CA.cert.filename': 'signer.pem',
                'proxy.config.ssl.client.alpn_protocols': 'h2,http/1.1',
                'proxy.config.ssl.client.verify.server.policy': policy,
                'proxy.config.ssl.client.verify.server.properties': 'ALL',
                'proxy.config.url_remap.pristine_host_hdr': 1,
                'proxy.config.http.server_session_sharing.pool': 'thread',
                'proxy.config.http.server_session_sharing.match': 'ip',
                'proxy.config.exec_thread.autoconfig.enabled': 0,
                'proxy.config.exec_thread.limit': 1,
                'proxy.config.diags.debug.enabled': int(policy == 'ENFORCED'),
                'proxy.config.diags.debug.tags': 'http|ssl_verify',
            })

        ts.Disk.ssl_multicert_yaml.AddLines(
            """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

        ts.Disk.remap_config.AddLine(f"map / https://127.0.0.1:{server.Variables.https_port}/")

    def _configure_requests(self, policy: str) -> None:
        ts = self._ts
        server = self._server

        tr = Test.AddTestRun(f"Prime an H2 origin connection for foo.com under {policy}")
        tr.Processes.Default.StartBefore(server)
        tr.Processes.Default.StartBefore(ts)
        tr.MakeCurlCommand(f"-v -H 'Host: foo.com' -H 'uuid: foo' http://127.0.0.1:{ts.Variables.port}/foo", ts=ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
            "foo-response", "foo.com should receive the origin response")
        tr.StillRunningAfter = server
        tr.StillRunningAfter = ts

        tr = Test.AddTestRun(f"Re-check the origin certificate for bar.com under {policy}")
        tr.MakeCurlCommand(f"-v -H 'Host: bar.com' -H 'uuid: foo' http://127.0.0.1:{ts.Variables.port}/foo", ts=ts)
        tr.Processes.Default.ReturnCode = 0
        if policy == 'ENFORCED':
            tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
                "Could Not Connect", "bar.com should fail certificate name verification")
        else:
            tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
                "foo-response", "PERMISSIVE should allow bar.com to reuse the origin session")
        tr.StillRunningAfter = server
        tr.StillRunningAfter = ts

    def _configure_logging_checks(self, policy: str) -> None:
        ts = self._ts

        if policy == 'ENFORCED':
            ts.Disk.traffic_out.Content = Testers.ContainsExpression(
                r"Origin hostname \(bar.com\) not in certificate for session reuse. Action=RejectReuse",
                "The pooled H2 origin session should be rejected for bar.com.")
            ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                "Origin hostname", "Rejected reuse candidates should not produce warnings.")
        else:
            ts.Disk.diags_log.Content += Testers.ContainsExpression(
                r"WARNING: Origin hostname \(bar.com\) not in certificate for session reuse. Action=ContinueReuse",
                "PERMISSIVE reuse mismatches should warn even with debug logging disabled.")
            ts.Disk.diags_log.Content += Testers.ExcludesExpression(
                r"SNI \(bar.com\) not in certificate", "bar.com should reuse the existing session without a new handshake.")


TestOriginCertReverify('ENFORCED')
TestOriginCertReverify('PERMISSIVE')
