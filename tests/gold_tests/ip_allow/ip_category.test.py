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

from typing import List

Test.Summary = '''
Verify IP allow ip_category behavior.
'''


class CategoryFile:
    """Encapsulate the various ip_category.yaml contents."""

    contents: List['CategoryFile'] = []
    parent_directory: str = Test.RunDirectory
    _index: int = 0

    def __init__(self, content: str):
        """Initialize the object.

        :param content: The content of the ip_category.yaml file.
        """
        self._content = content
        self._index = len(CategoryFile.contents)
        self._filename = os.path.join(CategoryFile.parent_directory, f'categories{self._index}.yaml')
        CategoryFile.contents.append(self)

    def _write(self):
        with open(self._filename, 'w') as f:
            f.write(self._content)

    def get_path(self):
        return self._filename

    @classmethod
    def write_all(cls):
        for content in cls.contents:
            content._write()


localhost_is_internal_and_external = CategoryFile(
    '''
ip_categories:
  - name: ACME_INTERNAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 127.0.0.1
  - name: ACME_ALL
    ip_addrs: 127.0.0.1
  - name: ALL
    ip_addrs: 127.0.0.1
''')

localhost_is_external = CategoryFile(
    '''
ip_categories:
  - name: ACME_INTERNAL
    ip_addrs: 1.2.3.4
  - name: ACME_REMAP_EXTERNAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 127.0.0.1
  - name: ACME_ALL
    ip_addrs:
      - 1.2.3.4
      - 127.0.0.1
  - name: ALL
    ip_addrs:
      - 1.2.3.4
      - 127.0.0.1
''')

localhost_is_neither = CategoryFile(
    '''
ip_categories:
  - name: ACME_INTERNAL
    ip_addrs: 1.2.3.4
  - name: ACME_EXTERNAL
    ip_addrs: 1.2.3.4
  - name: ACME_ALL
    ip_addrs: 1.2.3.4
  - name: ALL
    ip_addrs:
      - 1.2.3.4
      - 127.0.0.1
''')

# Keep this below the above content instantiations.
CategoryFile.write_all()


class Test_ip_category:
    """Configure a test to verify ip_category behavior."""

    _client_counter: int = 0
    _ts_is_started: bool = False
    _reload_server_is_started: bool = False
    _server_is_started: bool = False
    _reload_counter: int = 0

    _ts: 'TestProcess' = None
    _server: 'TestProcess' = None
    _reload_server: 'TestProcess' = None

    _categories_filename: str = f'{Test.RunDirectory}/categories.yaml'
    _category_files_are_written: bool = False

    _server_replay = 'replays/https_categories_server.replay.yaml'

    def __init__(
            self, name: str, replay_file: str, ip_allow_config: str, ip_category_config: 'CategoryFile', acl_configuration: str,
            expected_responses: List[int]):
        """Initialize the test.

        :param name: The name of the test.
        :param replay_file: The replay file to be used.
        :param ip_allow_config: The ip_allow configuration to be used.
        :param ip_category_config: The ip_category.yaml configuration to be used.
        :param acl_configuration: The ACL configuration to be used.
        :param expect_responses: The in-order expected responses from the proxy.
        """
        self._replay_file = replay_file
        self._ip_allow_config = ip_allow_config
        self._acl_configuration = acl_configuration
        self._expected_responses = expected_responses

        self._update_categories_file(ip_category_config)
        self._update_remap_with_acl()

        self._configure_server()
        self._configure_traffic_server()

        tr = Test.AddTestRun(name)
        self._configure_client(tr)

    def _update_remap_with_acl(self) -> None:
        """Update the remap.config file with the ACL configuration."""
        if Test_ip_category._ts:
            if self._acl_configuration:
                tr = Test.AddTestRun(f"remap.config file update with acl: {self._acl_configuration}")
                p = tr.Processes.Default
                destination = os.path.join(Test_ip_category._ts.Variables.CONFIGDIR, 'remap.config')
                common = f'map / http://127.0.0.1:{Test_ip_category._server.Variables.http_port} '
                p.Command = f'echo {common} {self._acl_configuration} > {destination}; cat {destination}'
                p.ReturnCode = 0
            else:
                tr = Test.AddTestRun(f"remap.config file update with no acl")
                p = tr.Processes.Default
                destination = os.path.join(Test_ip_category._ts.Variables.CONFIGDIR, 'remap.config')
                common = f'map / http://127.0.0.1:{Test_ip_category._server.Variables.http_port} '
                p.Command = f'echo {common} > {destination}; cat {destination}'
                p.ReturnCode = 0

    def _update_categories_file(self, category_content: 'CategoryFile') -> None:
        """Update the categories file.

        :param category_content: The content of the categories file.
        """
        tr = Test.AddTestRun(f"Categories file update: {category_content.get_path()}")
        p = tr.Processes.Default
        destination = Test_ip_category._categories_filename
        p.Command = f'cp {category_content.get_path()} {destination}; cat {destination}; ls -ltr {destination}'
        p.ReturnCode = 0

    def _configure_server(self) -> None:
        """Configure the server."""
        if Test_ip_category._server:
            # All test runs share a single server instance.
            return
        server = Test.MakeVerifierServerProcess(f"server", self._server_replay)
        Test_ip_category._server = server

    def _configure_traffic_server(self) -> None:
        """Configure Traffic Server."""

        if Test_ip_category._ts:
            # All test runs share a single Traffic Server instance.

            # Reload the ip_allow.yaml file.
            ts = Test_ip_category._ts
            tr = Test.AddTestRun(f"Reload the configuration file.")
            Test_ip_category._reload_counter += 1
            p = tr.Processes.Process(f"reload-{Test_ip_category._reload_counter}")
            # The sleep is added to give time for the reload to happen.
            p.Command = 'traffic_ctl config reload; sleep 30'
            p.Env = ts.Env
            # Killing the sleep can result in a -2 return code.
            p.ReturnCode = Any(0, -2)
            p.Ready = When.FileContains(
                ts.Disk.diags_log.Name, "ip_allow.yaml finished loading", 1 + Test_ip_category._reload_counter)
            p.Timeout = 20
            tr.StillRunningAfter = ts
            tr.Processes.Default.StartBefore(p)
            tr.Processes.Default.Command = 'echo "waiting upon traffic server to reload"'
            tr.TimeOut = 20

            return
        ts = Test.MakeATSProcess("ts", enable_cache=False, enable_tls=True)
        Test_ip_category._ts = ts

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|ip_allow',
                'proxy.config.cache.ip_categories.filename': Test_ip_category._categories_filename,
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.quic.no_activity_timeout_in': 0,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.http.connect_ports': Test_ip_category._server.Variables.http_port,
                'proxy.config.url_remap.acl_matching_policy': 1,  # TODO: adjust expected_responses with the default config
            })

        ts.Disk.remap_config.AddLine(
            f'map / http://127.0.0.1:{Test_ip_category._server.Variables.http_port} {self._acl_configuration}')
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
            f'client-{Test_ip_category._client_counter}', self._replay_file, https_ports=[Test_ip_category._ts.Variables.ssl_port])
        Test_ip_category._client_counter += 1

        if self._expected_responses:
            codes = [str(code) for code in self._expected_responses]
            p.Streams.stdout += Testers.ContainsExpression(
                '.*'.join(codes), "Verifying the expected order of responses", reflags=re.DOTALL | re.MULTILINE)
        else:
            # If there are no expected responses, expect the Warning about the rejected ip.
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "client '127.0.0.1' prohibited by ip-allow policy", "Verify the client rejection warning message.")

            # Also, the client will complain about the broken connections.
            p.ReturnCode = 1


IP_ALLOW_CONTENT = f'''
ip_allow:
  - apply: in
    ip_categories: ACME_INTERNAL
    action: allow
    methods:
      - GET
      - HEAD
      - POST
      - PUSH
  - apply: in
    ip_categories: ACME_EXTERNAL
    action: allow
    methods:
      - GET
      - HEAD
  - apply: in
    ip_categories: ACME_ALL
    action: allow
    methods:
      - HEAD
  - apply: in
    ip_categories: ALL
    action: deny
'''

test_ip_allow_optional_methods = Test_ip_category(
    "IP Category: INTERNAL",
    replay_file='replays/https_categories_internal.replay.yaml',
    ip_allow_config=IP_ALLOW_CONTENT,
    ip_category_config=localhost_is_internal_and_external,
    acl_configuration='',
    expected_responses=[200, 200, 400, 403])

test_ip_allow_optional_methods = Test_ip_category(
    "IP Category: EXTERNAL",
    replay_file='replays/https_categories_external.replay.yaml',
    ip_allow_config=IP_ALLOW_CONTENT,
    ip_category_config=localhost_is_external,
    acl_configuration='',
    expected_responses=[200, 403, 403])

# Because all requests are outright rejected for 127.0.0.1, ATS will
# reject all incoming transactions and not even give a 403 response.
test_ip_allow_optional_methods = Test_ip_category(
    "IP Category: ALL",
    replay_file='replays/https_categories_all.replay.yaml',
    ip_allow_config=IP_ALLOW_CONTENT,
    ip_category_config=localhost_is_neither,
    acl_configuration='',
    expected_responses=None)

# Deny GET via remap.config ACL.
test_ip_allow_optional_methods = Test_ip_category(
    "IP Category: EXTERNAL",
    replay_file='replays/https_categories_external_remap.replay.yaml',
    ip_allow_config=IP_ALLOW_CONTENT,
    ip_category_config=localhost_is_external,
    acl_configuration='@action=deny @src_ip_category=ACME_REMAP_EXTERNAL @method=GET',
    expected_responses=[403, 200, 200])
