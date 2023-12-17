'''
Verify IP allow ip_category behavior.
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
import re

Test.Summary = '''
Verify IP allow ip_category behavior.
'''

Test.ContinueOnFail = True


class Test_ip_category:
    """Configure a test to verify ip_category behavior."""

    _client_counter: int = 0
    _ts_is_started: bool = False
    _reload_server_is_started: bool = False
    _server_is_started: bool = False
    _category_counter: int = 0

    _ts: 'TestProcess' = None
    _server: 'TestProcess' = None
    _reload_server: 'TestProcess' = None

    _reload_server_replay = 'replays/https_categories_reload.replay.yaml'
    _server_replay = 'replays/https_categories_server.replay.yaml'

    _category_updater: str = 'category_updater.sh'
    _category_updater_path: str
    _have_copied_updater: bool = False

    def __init__(
            self, name: str, replay_file: str, ip_allow_config: str, localhost_categories: list[str], other_categories: list[str],
            expected_responses: list[int], is_h3: bool):
        """Initialize the test.

        :param name: The name of the test.
        :param replay_file: The replay file to be used.
        :param ip_allow_config: The ip_allow configuration to be used.
        :param localhost_categories: The categories to apply to the incoming connection.
        :param other_categories: The categories that are not applied to localhost.
        :param expect_responses: The in-order expected responses from the proxy.
        :param is_h3: Whether to use HTTP/3.
        """
        self._replay_file = replay_file
        self._ip_allow_config = ip_allow_config
        self._is_h3 = is_h3
        self._expected_responses = expected_responses

        self._update_categories_file(localhost_categories, other_categories)

        self._configure_reload_server()
        self._configure_server()
        self._configure_traffic_server()

        tr = Test.AddTestRun(name)
        self._configure_client(tr)

    def _update_categories_file(self, localhost_categories: list[str], other_categories: list[str]) -> None:
        """Update the categories file.

        :param localhost_categories: The categories to apply to the incoming connection.
        :param other_categories: The categories that are not applied to localhost.
        """

        if not Test_ip_category._have_copied_updater:
            Test.Setup.CopyAs(Test_ip_category._category_updater, Test.RunDirectory)
            Test_ip_category._have_copied_updater = True
            Test_ip_category._category_updater_path = os.path.join(Test.RunDirectory, Test_ip_category._category_updater)

        self._categories_file = os.path.join(Test.RunDirectory, 'categories.txt')
        category_updater = Test_ip_category._category_updater_path
        tr = Test.AddTestRun(f"Categories file update: {','.join(localhost_categories)}")

        name = f"category-server-{Test_ip_category._category_counter}"
        server = tr.Processes.Process(name)
        Test_ip_category._category_counter += 1
        server.Command = 'sleep 30'

        p = tr.Processes.Default
        p.Command = f'bash {category_updater} {self._categories_file} "{",".join(localhost_categories)}" "{",".join(other_categories)}"'
        p.StartBefore(server)

    def _configure_server(self) -> None:
        """Configure the server."""
        if Test_ip_category._server:
            # All test runs share a single server instance.
            return
        server = Test.MakeVerifierServerProcess(f"server", self._server_replay)
        Test_ip_category._server = server

    def _configure_reload_server(self) -> None:
        """Configure the server to handle the reload requests."""
        if Test_ip_category._reload_server:
            # All test runs share a single reload server instance.
            return
        server = Test.MakeVerifierServerProcess(f"reload_server", self._reload_server_replay)
        Test_ip_category._reload_server = server

    def _configure_traffic_server(self) -> None:
        """Configure Traffic Server."""
        if Test_ip_category._ts:
            # All test runs share a single Traffic Server instance.

            if Test_ip_category._category_counter > 1:
                # On subsequent runs, we have to tell the ip_category plugin to
                # reload the categories file.
                tr = Test.AddTestRun(f"HTTP request to tell the categories plugin to update the categories.")
                p = tr.Processes.Default
                p.Command = (
                    f"curl -v -H 'X-Category: reload' -H 'uuid: reload' "
                    f"http://127.0.0.1:{Test_ip_category._ts.Variables.port}/reload")
                if not Test_ip_category._reload_server_is_started:
                    p.StartBefore(Test_ip_category._reload_server)
                    Test_ip_category._reload_server_is_started = True
                p.Streams.all = Testers.ContainsExpression("200 OK", "Verify a 200 OK response from the reload server.")
                p.TimeOut = 5
            return
        ts = Test.MakeATSProcess("ts", enable_cache=False, enable_quic=self._is_h3, enable_tls=True)
        Test_ip_category._ts = ts

        ts.addDefaultSSLFiles()
        plugin_path = os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'ip_allow', 'plugins', '.libs', 'categories_from_file.so')
        Test.PrepareTestPlugin(plugin_path, ts, f'--category_file {self._categories_file}')
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'v_quic|quic|http|ip_allow|categories_from_file',
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.quic.no_activity_timeout_in': 0,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.http.connect_ports': Test_ip_category._server.Variables.http_port,
            })

        ts.Disk.remap_config.AddLines(
            [
                f'map /reload http://127.0.0.1:{Test_ip_category._reload_server.Variables.http_port}',
                f'map / http://127.0.0.1:{Test_ip_category._server.Variables.http_port}',
            ])

        ts.Disk.ip_allow_yaml.AddLines(self._ip_allow_config.split("\n"))

    def _configure_client(self, tr: 'TestRun') -> None:
        """Run the test.

        :param tr: The TestRun object to associate the client process with.
        """

        if not Test_ip_category._server_is_started:
            tr.Processes.Default.StartBefore(Test_ip_category._server)
            Test_ip_category._server_is_started = True
        if not Test_ip_category._ts_is_started:
            tr.Processes.Default.StartBefore(Test_ip_category._ts)
            Test_ip_category._ts_is_started = True

        p = tr.AddVerifierClientProcess(
            f'client-{Test_ip_category._client_counter}',
            self._replay_file,
            https_ports=[Test_ip_category._ts.Variables.ssl_port],
            http3_ports=[Test_ip_category._ts.Variables.ssl_port])
        Test_ip_category._client_counter += 1

        codes = [str(code) for code in self._expected_responses]
        p.Streams.stdout += Testers.ContainsExpression(
            '.*'.join(codes), "Verifying the expected order of responses", reflags=re.DOTALL | re.MULTILINE)


## ip_allow tests for h3.
#if Condition.HasATSFeature('TS_USE_QUIC') and Condition.HasCurlFeature('http3'):
#
#    # TEST 4: Perform a request in h3 with ip_allow configured to allow all IPs.
#    test0 = Test_ip_category(
#        "h3_allow_all",
#        replay_file='replays/h3.replay.yaml',
#        ip_allow_config=IP_ALLOW_CONFIG_ALLOW_ALL,
#        is_h3=True,
#        expect_request_rejected=False)
#    test0.run()
#
#    # TEST 5: Perform a request in h3 with ip_allow configured to deny all IPs.
#    test1 = Test_ip_category(
#        "h3_deny_all",
#        replay_file='replays/h3.replay.yaml',
#        ip_allow_config=IP_ALLOW_CONFIG_DENY_ALL,
#        is_h3=True,
#        expect_request_rejected=True)
#    test1.run()

# TEST 6: Verify rules are applied to all methods if methods is not specified.
IP_ALLOW_CONTENT = '''ip_allow:
  - apply: in
    ip_category: ACME_INTERNAL
    action: allow
    methods:
      - GET
      - HEAD
      - POST
      - PUSH
  - apply: in
    ip_category: ACME_EXTERNAL
    action: allow
    methods:
      - GET
      - HEAD
  - apply: in
    ip_category: ACME_ALL
    action: allow
    methods:
      - HEAD
  - apply: in
    ip_category: ALL
    action: deny
'''
test_ip_allow_optional_methods = Test_ip_category(
    "IP Category: INTERNAL",
    replay_file='replays/https_categories_internal.replay.yaml',
    ip_allow_config=IP_ALLOW_CONTENT,
    localhost_categories=['ACME_INTERNAL', 'ACME_ALL', 'ALL'],
    other_categories=['ACME_EXTERNAL'],
    is_h3=False,
    expected_responses=[200, 200, 400, 403])

test_ip_allow_optional_methods = Test_ip_category(
    "IP Category: EXTERNAL",
    replay_file='replays/https_categories_external.replay.yaml',
    ip_allow_config=IP_ALLOW_CONTENT,
    localhost_categories=['ACME_EXTERNAL', 'ACME_ALL', 'ALL'],
    other_categories=['ACME_INTERNAL'],
    is_h3=False,
    expected_responses=[200, 403, 403])

#test_ip_allow_optional_methods = Test_ip_category(
#    "IP Category: ALL",
#    replay_file='replays/https_categories_all.replay.yaml',
#    ip_allow_config=IP_ALLOW_CONTENT,
#    localhost_categories=['ACME_ALL', 'ALL'],
#    other_categories=['ACME_INTERNAL', 'ACME_EXTERNAL'],
#    is_h3=False,
#    expected_responses=[403, 403, 403])
