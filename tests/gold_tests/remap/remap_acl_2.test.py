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
from deactivate_ip_allow import all_deactivate_ip_allow_tests

Test.Summary = '''
Verify remap.config acl behavior.
'''


class Test_remap_acl:
    """Configure a test to verify remap.config acl behavior."""

    _ts_counter: int = 0
    _server_counter: int = 0
    _client_counter: int = 0

    def __init__(
            self, name: str, replay_file: str, ip_allow_content: str, deactivate_ip_allow: bool, acl_matching_policy: int,
            acl_configuration: str, named_acls: List[Tuple[str, str]], expected_responses: List[int]):
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
        self._acl_matching_policy = acl_matching_policy
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

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|url|remap|ip_allow',
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.http.connect_ports': self._server.Variables.http_port,
                'proxy.config.url_remap.acl_matching_policy': self._acl_matching_policy,
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

        remap_config_lines.append(f'map / http://127.0.0.1:{self._server.Variables.http_port}/ {self._acl_configuration}')
        ts.Disk.remap_config.AddLines(remap_config_lines)
        ts.Disk.ip_allow_yaml.AddLines(self._ip_allow_content.split("\n"))

    def _configure_client(self, tr: 'TestRun') -> None:
        """Run the test.

        :param tr: The TestRun object to associate the client process with.
        """

        name = f"client-{Test_remap_acl._client_counter}"
        p = tr.AddVerifierClientProcess(name, self._replay_file, http_ports=[self._ts.Variables.port])
        Test_remap_acl._client_counter += 1
        p.StartBefore(self._server)
        p.StartBefore(self._ts)

        if self._expected_responses == [None, None]:
            # If there are no expected responses, expect the Warning about the rejected ip.
            self._ts.Disk.diags_log.Content += Testers.ContainsExpression(
                "client '127.0.0.1' prohibited by ip-allow policy", "Verify the client rejection warning message.")

            # Also, the client will complain about the broken connections.
            p.ReturnCode = 1

        else:
            codes = [str(code) for code in self._expected_responses]
            p.Streams.stdout += Testers.ContainsExpression(
                '.*'.join(codes), "Verifying the expected order of responses", reflags=re.DOTALL | re.MULTILINE)


ALLOW_GET_AND_POST = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: allow
    methods: [GET, POST]
'''

ALLOW_GET = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: allow
    methods: [GET]
'''

DENY_GET = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: deny
    methods: [GET]
'''

DENY_GET_AND_POST = f'''
ip_allow:
  - apply: in
    ip_addrs: [0/0, ::/0]
    action: deny
    methods: [GET, POST]
'''

# From src/proxy/http/remap/RemapConfig.cc:123
# // ACLs are processed in this order:
# // 1. A remap.config ACL line for an individual remap rule.
# // 2. All named ACLs in remap.config.
# // 3. Rules as specified in ip_allow.yaml.
#
# If proxy.config.url_remap.acl_matching_policy is 0 (default), ATS employs "the first explicit match wins" policy. This means
# if it's implict match, ATS continue to process ACL filters. OTOH, if it's 1, ATS only process the first ACL filter.
#
# A simple example is:
#
# +--------+------------------+------------------+------------------+------------------+---------------+
# | Method |   remap.config   |    named ACL 1   |    named ACL 2   |  ip_allow.yaml   |    result     |
# +--------+------------------+------------------+------------------+------------------+---------------+
# | GET    | deny  (explicit) | -                | -                | -                | denied  (403) |
# | HEAD   | allow (implicit) | deny  (explicit) | -                | -                | denied  (403) |
# | POST   | allow (implicit) | allow (implicit) | allow (explicit) | -                | allowed (200) |
# | PUT    | allow (implicit) | allow (implicit) | deny  (implicit) | deny  (explicit) | deny    (403) |
# | DELETE | allow (implicit) | allow (implicit) | deny  (implicit) | allow (implicit) | allowed (200) |
# |--------+------------------+------------------+------------------+------------------+---------------+
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
                    transaction["proxy-response"]["status"] = 403 if get_proxy_response == None else get_proxy_response
                elif method == "POST":
                    transaction["proxy-response"]["status"] = 403 if post_proxy_response == None else post_proxy_response
                else:
                    raise Exception("Expected to find GET or POST request, found %s", method)
    with open(replay_file, "w") as f:
        f.write(dump(data))


# yapf: disable
keys = ["index", "policy", "inline", "named_acl", "ip_allow", "GET response", "POST response"]
all_acl_combinations = [
    [  0,  "explicit",  "",                          "",                           ALLOW_GET_AND_POST, 200, 200, ],
    [  1,  "explicit",  "",                          "",                           ALLOW_GET,          200, 403, ],
    [  2,  "explicit",  "",                          "",                           DENY_GET,           403, 200, ],
    [  3,  "explicit",  "",                          "",                           DENY_GET_AND_POST,  403, 403, ],
    [  4,  "explicit",  "",                          "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 200, ],
    [  5,  "explicit",  "",                          "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [  6,  "explicit",  "",                          "@action=allow @method=GET",  DENY_GET,           200, 200, ],
    [  7,  "explicit",  "",                          "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [  8,  "explicit",  "",                          "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [  9,  "explicit",  "",                          "@action=deny  @method=GET",  ALLOW_GET,          403, 403, ],
    [ 10,  "explicit",  "",                          "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 11,  "explicit",  "",                          "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 403, ],
    [ 12,  "explicit",  "@action=allow @method=GET", "",                           ALLOW_GET_AND_POST, 200, 200, ],
    [ 13,  "explicit",  "@action=allow @method=GET", "",                           ALLOW_GET,          200, 403, ],
    [ 14,  "explicit",  "@action=allow @method=GET", "",                           DENY_GET,           200, 200, ],
    [ 15,  "explicit",  "@action=allow @method=GET", "",                           DENY_GET_AND_POST,  200, 403, ],
    [ 16,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 200, ],
    [ 17,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [ 18,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET,           200, 200, ],
    [ 19,  "explicit",  "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 20,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 200, 200, ],
    [ 21,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          200, 403, ],
    [ 22,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET,           200, 200, ],
    [ 23,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 24,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", ALLOW_GET_AND_POST, 200, 200, ],
    [ 25,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", ALLOW_GET,          200, 200, ],
    [ 26,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", DENY_GET,           200, 200, ],
    [ 27,  "explicit",  "@action=allow @method=GET", "@action=allow @method=POST", DENY_GET_AND_POST,  200, 200, ],
    [ 28,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", ALLOW_GET_AND_POST, 200, 403, ],
    [ 29,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", ALLOW_GET,          200, 403, ],
    [ 30,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", DENY_GET,           200, 403, ],
    [ 31,  "explicit",  "@action=allow @method=GET", "@action=deny  @method=POST", DENY_GET_AND_POST,  200, 403, ],
    [ 32,  "explicit",  "@action=deny  @method=GET", "",                           ALLOW_GET_AND_POST, 403, 200, ],
    [ 33,  "explicit",  "@action=deny  @method=GET", "",                           ALLOW_GET,          403, 403, ],
    [ 34,  "explicit",  "@action=deny  @method=GET", "",                           DENY_GET,           403, 200, ],
    [ 35,  "explicit",  "@action=deny  @method=GET", "",                           DENY_GET_AND_POST,  403, 403, ],
    [ 36,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 37,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET,          403, 403, ],
    [ 38,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET,           403, 200, ],
    [ 39,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  403, 403, ],
    [ 40,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 41,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          403, 403, ],
    [ 42,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 43,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 403, ],
    [ 44,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", ALLOW_GET_AND_POST, 403, 200, ],
    [ 45,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", ALLOW_GET,          403, 200, ],
    [ 46,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", DENY_GET,           403, 200, ],
    [ 47,  "explicit",  "@action=deny  @method=GET", "@action=allow @method=POST", DENY_GET_AND_POST,  403, 200, ],
    [ 48,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", ALLOW_GET_AND_POST, 403, 403, ],
    [ 49,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", ALLOW_GET,          403, 403, ],
    [ 50,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", DENY_GET,           403, 403, ],
    [ 51,  "explicit",  "@action=deny  @method=GET", "@action=deny  @method=POST", DENY_GET_AND_POST,  403, 403, ],
    [ 52,  "any",       "",                          "",                           ALLOW_GET_AND_POST, 200, 200, ],
    [ 53,  "any",       "",                          "",                           ALLOW_GET,          200, 403, ],
    [ 54,  "any",       "",                          "",                           DENY_GET,           403, 200, ],
    [ 55,  "any",       "",                          "",                           DENY_GET_AND_POST,  403, 403, ],
    [ 56,  "any",       "",                          "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 403, ],
    [ 57,  "any",       "",                          "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [ 58,  "any",       "",                          "@action=allow @method=GET",  DENY_GET,           200, 403, ],
    [ 59,  "any",       "",                          "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 60,  "any",       "",                          "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 61,  "any",       "",                          "@action=deny  @method=GET",  ALLOW_GET,          403, 200, ],
    [ 62,  "any",       "",                          "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 63,  "any",       "",                          "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 200, ],
    [ 64,  "any",       "@action=allow @method=GET", "",                           ALLOW_GET_AND_POST, 200, 403, ],
    [ 65,  "any",       "@action=allow @method=GET", "",                           ALLOW_GET,          200, 403, ],
    [ 66,  "any",       "@action=allow @method=GET", "",                           DENY_GET,           200, 403, ],
    [ 67,  "any",       "@action=allow @method=GET", "",                           DENY_GET_AND_POST,  200, 403, ],
    [ 68,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 200, 403, ],
    [ 69,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  ALLOW_GET,          200, 403, ],
    [ 70,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET,           200, 403, ],
    [ 71,  "any",       "@action=allow @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 72,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 200, 403, ],
    [ 73,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          200, 403, ],
    [ 74,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET,           200, 403, ],
    [ 75,  "any",       "@action=allow @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  200, 403, ],
    [ 76,  "any",       "@action=deny  @method=GET", "",                           ALLOW_GET_AND_POST, 403, 200, ],
    [ 77,  "any",       "@action=deny  @method=GET", "",                           ALLOW_GET,          403, 200, ],
    [ 78,  "any",       "@action=deny  @method=GET", "",                           DENY_GET,           403, 200, ],
    [ 79,  "any",       "@action=deny  @method=GET", "",                           DENY_GET_AND_POST,  403, 200, ],
    [ 80,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 81,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  ALLOW_GET,          403, 200, ],
    [ 82,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET,           403, 200, ],
    [ 83,  "any",       "@action=deny  @method=GET", "@action=allow @method=GET",  DENY_GET_AND_POST,  403, 200, ],
    [ 84,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET_AND_POST, 403, 200, ],
    [ 85,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  ALLOW_GET,          403, 200, ],
    [ 86,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET,           403, 200, ],
    [ 87,  "any",       "@action=deny  @method=GET", "@action=deny  @method=GET",  DENY_GET_AND_POST,  403, 200, ],
]
# yapf: enable

all_acl_combination_tests = [dict(zip(keys, test)) for test in all_acl_combinations]
"""
Test all acl combinations
"""
for idx, test in enumerate(all_acl_combination_tests):
    (_, replay_file_name) = tempfile.mkstemp(suffix="acl_table_test_{}.replay".format(idx))
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
        acl_matching_policy=0 if test["policy"] == "explicit" else 1,
        acl_configuration=test["inline"],
        named_acls=[("acl", test["named_acl"])] if test["named_acl"] != "" else [],
        expected_responses=[test["GET response"], test["POST response"]],
    )

"""
Test all ACL combinations
"""
for idx, test in enumerate(all_deactivate_ip_allow_tests):
    try:
        test["deactivate_ip_allow"]
    except:
        print(test)
    (_, replay_file_name) = tempfile.mkstemp(suffix="deactivate_ip_allow_table_test_{}.replay".format(idx))
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
        deactivate_ip_allow=test["deactivate_ip_allow"],
        acl_matching_policy=0 if test["policy"] == "explicit" else 1,
        acl_configuration=test["inline"],
        named_acls=[("acl", test["named_acl"])] if test["named_acl"] != "" else [],
        expected_responses=[test["GET response"], test["POST response"]]
    )
