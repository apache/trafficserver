'''
Regression test for the ESI <!--esi ... --> comment wrapper handling.

Covers the sec-035 fix that rejects nested <!--esi ... --> wrappers inside
another <!--esi ... -->. Two scenarios:

  1. Legitimate single-level wrapper: must still be expanded normally and
     the nested-wrapper guard must NOT fire.

  2. Nested wrapper hidden inside <esi:try>/<esi:attempt> child_nodes:
     must be rejected by the guard and the diags log must record the
     "Nested <!--esi ...--> inside <!--esi ...-->" error. This is the
     deeper case the prior top-level-only check missed.
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
Verify the ESI plugin processes a legitimate single-level <!--esi ...--> wrapper
and rejects a nested <!--esi ...--> hidden inside <esi:try>/<esi:attempt>.
'''

Test.SkipUnless(Condition.PluginExists('esi.so'),)


class EsiHtmlCommentTest():
    """
    Drive a single request through ATS whose origin response contains an
    `<!--esi <esi:vars>...</esi:vars>-->` wrapper, and verify the plugin
    expands it without tripping the nested-wrapper guard.
    """

    _replay_file: str = "esi_nested_html_comment.replay.yaml"

    def __init__(self, plugin_config: str) -> None:
        tr = Test.AddTestRun("ESI single-level <!--esi--> wrapper is processed")
        self._create_server(tr)
        self._create_ats(tr, plugin_config)
        self._create_client(tr)

    def _create_server(self, tr: 'TestRun') -> 'Process':
        server = tr.AddVerifierServerProcess("server", self._replay_file, other_args='--format "{url}"')
        self._server = server

        server.Streams.All += Testers.ContainsExpression(
            'GET /esi-html-comment.php', 'Verify the server received the ESI document request.')
        return server

    def _create_ats(self, tr: 'TestRun', plugin_config: str) -> 'Process':
        ts = tr.MakeATSProcess("ts")
        self._ts = ts
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|plugin_esi|plugin_esi_procesor',
            })
        server_port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map http://www.example.com/ http://127.0.0.1:{server_port}')
        ts.Disk.plugin_config.AddLine(plugin_config)

        # The nested-wrapper guard added for sec-035 must NOT fire on a
        # legitimate single-level <!--esi ... --> wrapper.
        ts.Disk.diags_log.Content = Testers.ExcludesExpression(
            r'Nested <!--esi \.\.\.--> inside <!--esi \.\.\.-->', 'The nested-wrapper guard must not fire on legitimate input.')
        return ts

    def _create_client(self, tr: 'TestRun') -> None:
        p = tr.AddVerifierClientProcess(
            "client",
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            other_args='--format "{url}" --keys /esi-html-comment.php')
        p.ReturnCode = 0
        p.StartBefore(self._server)
        p.StartBefore(self._ts)

        # The body must contain the expanded variable from inside the
        # <!--esi ... --> wrapper.
        p.Streams.stdout += Testers.ContainsExpression('www.example.com', 'Verify the client received the expanded ESI body.')


class EsiNestedHtmlCommentRejectTest():
    """
    Drive a request whose origin response contains a nested <!--esi ...-->
    hidden inside <esi:try>/<esi:attempt> child_nodes, and verify the
    processor rejects it via the raw-substring guard. The prior fix only
    scanned the top level of inner_nodes and would have let this through.
    """

    _replay_file: str = "esi_nested_html_comment_reject.replay.yaml"

    def __init__(self, plugin_config: str) -> None:
        tr = Test.AddTestRun("ESI nested <!--esi--> wrapper is rejected")
        self._create_server(tr)
        self._create_ats(tr, plugin_config)
        self._create_client(tr)

    def _create_server(self, tr: 'TestRun') -> 'Process':
        server = tr.AddVerifierServerProcess("server-reject", self._replay_file, other_args='--format "{url}"')
        self._server = server

        server.Streams.All += Testers.ContainsExpression(
            'GET /esi-nested-reject.php', 'Verify the server received the nested-wrapper request.')
        return server

    def _create_ats(self, tr: 'TestRun', plugin_config: str) -> 'Process':
        ts = tr.MakeATSProcess("ts-reject")
        self._ts = ts
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|plugin_esi|plugin_esi_procesor',
            })
        server_port = self._server.Variables.http_port
        ts.Disk.remap_config.AddLine(f'map http://www.example.com/ http://127.0.0.1:{server_port}')
        ts.Disk.plugin_config.AddLine(plugin_config)

        # The guard must fire on a nested wrapper, even when the nested
        # <!--esi ...--> is hidden inside <esi:try>/<esi:attempt> rather
        # than appearing at the top level of the outer wrapper's content.
        ts.Disk.diags_log.Content = Testers.ContainsExpression(
            r'Nested <!--esi \.\.\.--> inside <!--esi \.\.\.--> is not allowed',
            'The nested-wrapper guard must fire on a nested ESI comment hidden inside <esi:try>/<esi:attempt>.')
        return ts

    def _create_client(self, tr: 'TestRun') -> None:
        p = tr.AddVerifierClientProcess(
            "client-reject",
            self._replay_file,
            http_ports=[self._ts.Variables.port],
            other_args='--format "{url}" --keys /esi-nested-reject.php')
        p.ReturnCode = 0
        p.StartBefore(self._server)
        p.StartBefore(self._ts)
        # The strong signal that the guard fired is the diags_log
        # ContainsExpression in _create_ats. We deliberately do not
        # assert on the client body here, since the failure path
        # (pass-through vs. error response) is orthogonal to the
        # guard and may evolve independently.


EsiHtmlCommentTest(plugin_config='esi.so')
EsiNestedHtmlCommentRejectTest(plugin_config='esi.so')
