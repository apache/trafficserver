'''
Verify remap.config acl behavior.
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
import io
import re
import pathlib
import inspect
import tempfile
from yaml import load, dump
from yaml import CLoader as Loader
from typing import List, Tuple

Test.Summary = '''
Verify remap.config acl behavior.
'''


class Test_remap_acl:
    """Configure a test to verify remap.config acl behavior."""

    _ts_counter: int = 0
    _server_counter: int = 0
    _client_counter: int = 0

    def __init__(
            self, name: str, replay_file: str, ip_allow_content: str, deactivate_ip_allow: bool, acl_configuration: str,
            named_acls: List[Tuple[str, str]], expected_responses: List[int]):
        """Initialize the test.

        :param name: The name of the test.
        :param replay_file: The replay file to be used.
        :param ip_allow_content: The ip_allow configuration to be used.
        :param deactivate_ip_allow: Whether to deactivate the ip_allow filter.
        :param acl_configuration: The ACL configuration to be used.
        :param named_acls: The set of named ACLs to configure and use.
        :param expect_responses: The in-order expected responses from the proxy.
        """
        self._replay_file = replay_file
        self._ip_allow_content = ip_allow_content
        self._deactivate_ip_allow = deactivate_ip_allow
        self._acl_configuration = acl_configuration
        self._named_acls = named_acls
        self._expected_responses = expected_responses

        tr = Test.AddTestRun(name)
        self._configure_server(tr)
        self._configure_traffic_server(tr)
        self._configure_client(tr)

    def _configure_server(self, tr: 'TestRun') -> None:
        """Configure the server.

        :param tr: The TestRun object to associate the server process with.
        """
        name = f"server-{Test_remap_acl._server_counter}"
        server = tr.AddVerifierServerProcess(name, self._replay_file)
        Test_remap_acl._server_counter += 1
        self._server = server

    def _configure_traffic_server(self, tr: 'TestRun') -> None:
        """Configure Traffic Server.

        :param tr: The TestRun object to associate the Traffic Server process with.
        """

        name = f"ts-{Test_remap_acl._ts_counter}"
        ts = tr.MakeATSProcess(name, enable_cache=False, enable_tls=True)
        Test_remap_acl._ts_counter += 1
        self._ts = ts

        ts.addDefaultSSLFiles()
        ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')
        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|url|remap|ip_allow',
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
                'proxy.config.quic.no_activity_timeout_in': 0,
                'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
                'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
                'proxy.config.http.connect_ports': self._server.Variables.http_port,
            })

        remap_config_lines = []
        if self._deactivate_ip_allow:
            remap_config_lines.append('.deactivatefilter ip_allow')

        # First, define the name ACLs (filters).
        for name, definition in self._named_acls:
            remap_config_lines.append(f'.definefilter {name} {definition}')
        # Now activate them.
        for name, _ in self._named_acls:
            remap_config_lines.append(f'.activatefilter {name}')

        remap_config_lines.append(f'map / http://127.0.0.1:{self._server.Variables.http_port} {self._acl_configuration}')
        ts.Disk.remap_config.AddLines(remap_config_lines)
        ts.Disk.ip_allow_yaml.AddLines(self._ip_allow_content.split("\n"))

    def _configure_client(self, tr: 'TestRun') -> None:
        """Run the test.

        :param tr: The TestRun object to associate the client process with.
        """

        name = f"client-{Test_remap_acl._client_counter}"
        p = tr.AddVerifierClientProcess(name, self._replay_file, https_ports=[self._ts.Variables.ssl_port])
        Test_remap_acl._client_counter += 1
        p.StartBefore(self._server)
        p.StartBefore(self._ts)

        codes = [str(code) for code in self._expected_responses]
        p.Streams.stdout += Testers.ContainsExpression(
            '.*'.join(codes), "Verifying the expected order of responses", reflags=re.DOTALL | re.MULTILINE)


IP_ALLOW_CONTENT = f'''
ip_categories:
  - name: ACME_LOCAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 5.6.7.8

ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: allow
    methods:
      - GET
'''

IP_ALLOW_DENY = f'''
ip_categories:
  - name: ACME_LOCAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 5.6.7.8

ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: deny
    methods:
      - GET
'''

IP_ALLOW_MINIMUM = f'''
ip_categories:
  - name: ACME_LOCAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 5.6.7.8
ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: allow
'''

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify non-allowed methods are blocked.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify if no ACLs match, ip_allow.yaml is used.",
    replay_file='remap_acl_get_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 403, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify @src_ip=all works.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=all @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify @src_ip_category works.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip_category=ACME_LOCAL @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify no @src_ip implies all IP addresses.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify denied methods are blocked.",
    replay_file='remap_acl_get_post_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=deny @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[403, 403, 200, 200, 400])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify a default deny filter rule works.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[('deny', '@action=deny')],
    expected_responses=[403, 403, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip works.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=~127.0.0.1 @method=GET @method=POST',
    named_acls=[('deny', '@action=deny')],
    expected_responses=[403, 403, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip works with the rule matching.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=~3.4.5.6 @method=GET @method=POST',
    named_acls=[('deny', '@action=deny')],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip_category works.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip_category=~ACME_LOCAL @method=GET @method=POST',
    named_acls=[('deny', '@action=deny')],
    expected_responses=[403, 403, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip_category works with the rule matching.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip_category=~ACME_EXTERNAL @method=GET @method=POST',
    named_acls=[('deny', '@action=deny')],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify @src_ip and @src_ip_category AND together.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    # The rule will not match because, while @src_ip matches, @src_ip_category does not.
    acl_configuration='@action=allow @src_ip=127.0.0.1 @src_ip_category=ACME_EXTERNAL @method=GET @method=POST',
    # Therefore, this named deny filter will block.
    named_acls=[('deny', '@action=deny')],
    expected_responses=[403, 403, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify defined in-line ACLS are evaluated before named ones.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[('deny', '@action=deny')],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify remap.config line overrides ip_allow rule.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify we can deactivate the ip_allow filter.",
    replay_file='remap_acl_all_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=True,
    # This won't match, so nothing will match since ip_allow.yaml is off.
    acl_configuration='@action=allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[],
    # Nothing will block the request since ip_allow.yaml is off.
    expected_responses=[200, 200, 200, 200, 400])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify in_ip matches on IP as expected.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @in_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403])

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify in_ip rules do not match on other IPs.",
    replay_file='remap_acl_get_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='@action=allow @in_ip=3.4.5.6 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 403, 403, 403, 403])

test_named_acl_deny = Test_remap_acl(
    "Verify a named ACL is applied if an in-line ACL is absent.",
    replay_file='deny_head_post.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_configuration='',
    named_acls=[('deny', '@action=deny @method=HEAD @method=POST')],
    expected_responses=[200, 403, 403, 403])
'''
From src/proxy/http/remap/RemapConfig.cc:123
// ACLs are processed in this order:
// 1. A remap.config ACL line for an individual remap rule.
// 2. All named ACLs in remap.config.
// 3. Rules as specified in ip_allow.yaml.


A simple example is:
+--------+--------------+------------------+------------------+---------------+
| Method | remap.config |    named ACL     |  ip_allow.yaml   |    result     |
+--------+--------------+------------------+------------------+---------------+
| GET    |            - | allow (implicit) | allow (explicit) | allowed (200) |
| HEAD   |            - | deny (explicit)  | -                | denied (403)  |
| POST   |            - | deny (explicit)  | -                | denied (403)  |
| PUT    |            - | allow (implicit) | deny (implicit)  | allowed (200) |
| DELETE |            - | allow (implicit) | deny (implicit)  | allowed (200) |
| PUSH   |            - | allow (implicit) | deny (implicit)  | allowed (200) |
|--------+--------------+------------------+------------------+---------------+

'''
'''
replay_proxy_response writes the given replay file (which expects a single GET & POST client-request)
with the given proxy_response value. This is only used to support the below table test.
'''


def replay_proxy_response(filename, replay_file, get_proxy_response, post_proxy_response):
    current_dir = os.path.dirname(inspect.getfile(inspect.currentframe()))
    path = os.path.join(current_dir, filename)
    data = None
    with open(path) as f:
        data = load(f, Loader=Loader)
        for session in data["sessions"]:
            for transaction in session["transactions"]:
                method = transaction["client-request"]["method"]
                if method == "GET":
                    transaction["proxy-response"]["status"] = get_proxy_response
                elif method == "POST":
                    transaction["proxy-response"]["status"] = post_proxy_response
                else:
                    raise Exception("Expected to find GET or POST request, found %s", method)
    with open(replay_file, "w") as f:
        f.write(dump(data))


all_acl_combinations = [
    {
        "inline": "",
        "named_acl": "",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 200,
        "POST response": 200,
    },
    {
        "inline": "",
        "named_acl": "",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "",
        "named_acl": "",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=allow @method=GET",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 200,
        "POST response": 403,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "@action=allow @method=GET",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_MINIMUM,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_CONTENT,
        "GET response": 403,
        "POST response": 200,
    },
    {
        "inline": "@action=deny @method=GET",
        "named_acl": "@action=deny @method=GET",
        "ip_allow": IP_ALLOW_DENY,
        "GET response": 403,
        "POST response": 200,
    },
]
"""
Test all acl combinations
"""
for idx, test in enumerate(all_acl_combinations):
    (_, replay_file_name) = tempfile.mkstemp(suffix="table_test_{}.replay".format(idx))
    replay_proxy_response(
        "base.replay.yaml",
        replay_file_name,
        test["GET response"],
        test["POST response"],
    )
    Test.Summary = "table test {0}".format(idx)
    Test_remap_acl(
        "{0} {1} {2}".format(test["inline"], test["named_acl"], test["ip_allow"]),
        replay_file=replay_file_name,
        ip_allow_content=test["ip_allow"],
        deactivate_ip_allow=False,
        acl_configuration=test["inline"],
        named_acls=[("acl", test["named_acl"])] if test["named_acl"] != "" else [],
        expected_responses=[test["GET response"], test["POST response"]],
    )
