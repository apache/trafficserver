'''
Test SSRF validation for ESI include src= URLs.
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
Verify the ESI plugin's SSRF guard rejects include URLs with private-IP
hosts, non-http(s) schemes, and attacker-controlled variable expansion,
while still allowing ordinary includes to the configured upstream.
'''

Test.SkipUnless(Condition.PluginExists('esi.so'),)


class EsiSsrfTest:
    """Drives the replay file through ATS with the SSRF guard enabled and
    confirms each rejection path logs the expected reason while the
    allowed include is still fetched and inlined."""

    _replay_file: str = "esi_ssrf_validate.replay.yaml"

    def __init__(self) -> None:
        tr = Test.AddTestRun("ESI include URLs are validated for SSRF")
        self._create_server(tr)
        self._create_ats(tr)
        self._create_client(tr)

    def _create_server(self, tr: 'TestRun') -> None:
        server = tr.AddVerifierServerProcess("server", self._replay_file, other_args='--format "{url}"')
        self._server = server

        # The snippet for the allowed-include case must reach the origin.
        # The rejected cases must never reach any backend other than the
        # five top-level documents themselves; we don't assert their
        # absence on the server stream because the rejected hosts are
        # not mapped to this verifier.
        server.Streams.All += Testers.ContainsExpression('GET /allowed.php', 'Verify the allowed top-level request reached origin.')
        server.Streams.All += Testers.ContainsExpression(
            'GET /snippet.html', 'Verify the snippet for the allowed include was fetched.')

    def _create_ats(self, tr: 'TestRun') -> None:
        ts = tr.MakeATSProcess("ts")
        self._ts = ts
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|plugin_esi',
        })

        server_port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map http://www.example.com/ http://127.0.0.1:{server_port}')

        # Default-deny private/loopback hosts; no allow-regex configured,
        # so any non-private host is permitted (the upstream maps via
        # www.example.com).
        ts.Disk.plugin_config.AddLine('esi.so')

        # Each rejection must be logged with its reason. Reason strings
        # come from IncludeUrlValidator::reasonString().
        # TSError emits one line per rejection that contains both the
        # offending URL substring and the reason token. Match each pair
        # loosely so the assertion survives bracket/paren formatting
        # differences in the diags log. ``.`` is newline-bounded by
        # default, so each regex stays single-line.
        ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r'Rejecting include URL.*169\.254\.169\.254.*private-host',
            'Cloud-metadata IPv4 literal must be rejected as private-host.')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rejecting include URL.*127\.0\.0\.1/admin.*private-host', 'Loopback IPv4 literal must be rejected as private-host.')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rejecting include URL.*gopher://internal\.svc/x.*bad-scheme', 'Non-http(s) scheme must be rejected as bad-scheme.')
        ts.Disk.diags_log.Content += Testers.ContainsExpression(
            r'Rejecting include URL.*10\.0\.0\.5/secret.*private-host',
            'Attacker-influenced variable expansion must be rejected after \\$\\(...\\) is expanded.')

        # The allowed case must NOT show up as a rejection.
        ts.Disk.diags_log.Content += Testers.ExcludesExpression(
            r'Rejecting include URL.*www\.example\.com/snippet\.html',
            'The legitimate include to the mapped upstream must not be rejected.')

    def _create_client(self, tr: 'TestRun') -> None:
        # Proxy Verifier's default --keys format in this codebase keys
        # transactions by URL (see esi_nested_include.test.py), not by the
        # `uuid` request header. Match on the top-level document URLs so
        # the client drives the four rejection cases and the one allowed
        # case; the /snippet.html transaction is omitted because ATS
        # fetches it internally via the ESI include.
        p = tr.AddVerifierClientProcess(
            "client",
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            other_args='--format "{url}" --keys /metadata.php /loopback.php /badscheme.php /varinject.php /allowed.php')
        p.ReturnCode = 0
        p.StartBefore(self._server)
        p.StartBefore(self._ts)


EsiSsrfTest()
