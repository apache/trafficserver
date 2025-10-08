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
# #      http://www.apache.org/licenses/LICENSE-2.0 #
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import os
import io
import re
import inspect
import tempfile
from yaml import load, dump
from yaml import CLoader as Loader
from typing import List, Tuple

from ports import get_port

Test.Summary = '''
Verify remap.config acl behavior.
'''


def update_config_file(path1: str, content1: str, path2: str, content2: str) -> None:
    """Update two config files.

    This is used for some of the updates to the config files between test runs.

    :param path1: The path to the first config file.
    :param content1: The content to write to the first config file.
    :param path2: The path to the second config file.
    :param content2: The content to write to the second config file.
    """
    with open(path1, 'w') as f:
        f.write(content1 + '\n')
    with open(path2, 'w') as f:
        f.write(content2 + '\n')


class Test_remap_acl:
    """Configure a test to verify remap.config acl behavior."""

    _ts: 'TestProcess' = None
    _ts_reload_counter: int = 0
    _ts_is_started: bool = False

    _server_counter: int = 0
    _client_counter: int = 0

    def __init__(
            self, name: str, replay_file: str, ip_allow_content: str, deactivate_ip_allow: bool, acl_behavior_policy: int,
            acl_configuration: str, named_acls: List[Tuple[str, str]], expected_responses: List[int], proxy_protocol: bool):
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
        self._ip_allow_lines = ip_allow_content.split("\n")
        self._deactivate_ip_allow = deactivate_ip_allow
        self._acl_behavior_policy = acl_behavior_policy
        self._acl_configuration = acl_configuration
        self._named_acls = named_acls
        self._expected_responses = expected_responses

        # Usually we configure the server first and use the server port to
        # configure ATS to remap to it. In this case, though, we want a
        # long-lived ATS process that spans TestRuns. So we let ATS choose an
        # arbitrary availble server port, and then tell the TestRun-specific
        # server to use that port.
        server_port = self._configure_traffic_server()
        tr = Test.AddTestRun(name)
        self._configure_server(tr, server_port)
        self._configure_client(tr, proxy_protocol)

    def _configure_server(self, tr: 'TestRun', server_port: int) -> None:
        """Configure the server.
        """
        name = f"server-{Test_remap_acl._server_counter}"
        server = tr.AddVerifierServerProcess(name, self._replay_file, http_ports=[server_port])
        Test_remap_acl._server_counter += 1
        self._server = server

    def _configure_traffic_server(self) -> int:
        """Configure Traffic Server.

        :return: The listening port that the server should use.
        """

        call_reload: bool = False
        if Test_remap_acl._ts is not None:
            ts = Test_remap_acl._ts
            call_reload = True
        else:
            ts = Test.MakeATSProcess("ts", enable_cache=False, enable_proxy_protocol=True, enable_uds=False)
            Test_remap_acl._ts = ts
        self._ts = ts
        port_name = f'ServerPort-{Test_remap_acl._ts_reload_counter}'
        server_port: int = get_port(ts, port_name)

        remap_config_lines = []
        if self._deactivate_ip_allow:
            remap_config_lines.append('.deactivatefilter ip_allow')

        # First, define the name ACLs (filters).
        for name, definition in self._named_acls:
            remap_config_lines.append(f'.definefilter {name} {definition}')
        # Now activate them.
        for name, _ in self._named_acls:
            remap_config_lines.append(f'.activatefilter {name}')

        remap_config_lines.append(f'map / http://127.0.0.1:{server_port} {self._acl_configuration}')

        if call_reload:
            #
            # Update the ATS configuration.
            #
            tr = Test.AddTestRun("Change the ATS configuration")
            p = tr.Processes.Default
            p.Command = (
                f'traffic_ctl config set proxy.config.http.connect_ports {server_port} && '
                f'traffic_ctl config set proxy.config.url_remap.acl_behavior_policy {self._acl_behavior_policy}')

            p.Env = ts.Env
            tr.StillRunningAfter = ts

            remap_cfg_path = os.path.join(ts.Variables.CONFIGDIR, 'remap.config')
            ip_allow_path = os.path.join(ts.Variables.CONFIGDIR, 'ip_allow.yaml')
            p.Setup.Lambda(
                lambda: update_config_file(
                    remap_cfg_path, '\n'.join(remap_config_lines), ip_allow_path, '\n'.join(self._ip_allow_lines)))

            #
            # Kick off the ATS config reload.
            #
            tr = Test.AddTestRun("Reload the ATS configuration")
            p = tr.Processes.Default
            p.Command = 'traffic_ctl config reload'
            p.Env = ts.Env
            tr.StillRunningAfter = ts

            #
            # Await the config reload to finish.
            #
            tr = Test.AddTestRun("Await config reload")
            p = tr.Processes.Default
            p.Command = 'echo awaiting config reload'
            p.Env = ts.Env
            Test_remap_acl._ts_reload_counter += 1
            count = Test_remap_acl._ts_reload_counter
            await_config_reload = tr.Processes.Process(f'config_reload_succeeded_{count}', 'sleep 30')
            await_config_reload.Ready = When.FileContains(ts.Disk.diags_log.Name, "remap.config finished loading", count)
            p.StartBefore(await_config_reload)

        else:
            record_config = {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|url|remap|ip_allow|proxyprotocol',
                'proxy.config.http.push_method_enabled': 1,
                'proxy.config.http.connect_ports': server_port,
                'proxy.config.url_remap.acl_behavior_policy': self._acl_behavior_policy,
                'proxy.config.acl.subjects': 'PROXY,PEER',
            }

            ts.Disk.records_config.update(record_config)
            ts.Disk.remap_config.AddLines(remap_config_lines)
            ts.Disk.ip_allow_yaml.AddLines(self._ip_allow_lines)

        return server_port

    def _configure_client(self, tr: 'TestRun', proxy_protocol: bool) -> None:
        """Run the test.

        :param tr: The TestRun object to associate the client process with.
        """

        name = f"client-{Test_remap_acl._client_counter}"
        ts = Test_remap_acl._ts
        port = ts.Variables.port if proxy_protocol == False else ts.Variables.proxy_protocol_port
        p = tr.AddVerifierClientProcess(name, self._replay_file, http_ports=[port])
        Test_remap_acl._client_counter += 1
        p.StartBefore(self._server)
        if not Test_remap_acl._ts_is_started:
            p.StartBefore(ts)
            Test_remap_acl._ts_is_started = True

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


class Test_old_action:
    _ts_counter: int = 0

    def __init__(self, name: str, acl_filter: str, ip_allow_content: str) -> None:
        '''Test that ATS fails with a FATAL message if an old action is used with modern ACL filter policy.

        :param name: The name of the test run.
        :param acl_filter: The ACL filter to use.
        :param ip_allow_content: The ip_allow configuration to use.
        '''

        tr = Test.AddTestRun(name)
        ts = self._configure_traffic_server(tr, acl_filter, ip_allow_content)

    def _configure_traffic_server(self, tr: 'TestRun', acl_filter: str, ip_allow_content: str) -> 'Process':
        '''Configure Traffic Server process

        :param tr: The TestRun object to associate the Traffic Server process with.
        :param acl_filter: The ACL filter to configure in remap.config.
        :param ip_allow_content: The ip_allow configuration to use.
        :return: The Traffic Server process.
        '''
        name = f"ts-old-action-{Test_old_action._ts_counter}"
        Test_old_action._ts_counter += 1
        ts = tr.MakeATSProcess(name, enable_uds=False)
        self._ts = ts

        ts.Disk.records_config.update(
            {
                'proxy.config.diags.debug.enabled': 1,
                'proxy.config.diags.debug.tags': 'http|url|remap|ip_allow',
                'proxy.config.url_remap.acl_behavior_policy': 1,
            })

        ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:8080 {acl_filter}')
        if ip_allow_content:
            ts.Disk.ip_allow_yaml.AddLines(ip_allow_content.split("\n"))

        if acl_filter != '':
            expected_error = '"allow" and "deny" are no longer valid.'
        else:
            expected_error = 'Legacy action name of'

        # We have to wait upon TS to emit the expected log message, but it cannot be
        # the ts Ready criteria because autest might detect the process going away
        # before it detects the log message. So we add a separate process that waits
        # upon the log message.
        watcher = tr.Processes.Process("watcher")
        watcher.Command = "sleep 10"
        watcher.Ready = When.FileContains(ts.Disk.diags_log.Name, expected_error)
        watcher.StartBefore(ts)

        tr.Processes.Default.Command = 'printf "Fatal Shutdown Test"'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(watcher)

        tr.Timeout = 5
        ts.ReturnCode = Any(33, 70)
        ts.Ready = 0
        ts.Disk.diags_log.Content = Testers.IncludesExpression(expected_error, 'ATS should fatal with the old actions.')

        return ts


IP_ALLOW_OLD_ACTION = f'''
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

IP_ALLOW_CONTENT = f'''
ip_categories:
  - name: ACME_LOCAL
    ip_addrs: 127.0.0.1
  - name: ACME_EXTERNAL
    ip_addrs: 5.6.7.8

ip_allow:
  - apply: in
    ip_addrs: 0/0
    action: set_allow
    methods:
      - GET
'''

Test_old_action("Verify allow is reject in modern policy", "@action=allow @method=GET", IP_ALLOW_CONTENT)
Test_old_action("Verify deny is reject in modern policy", "@action=deny @method=GET", IP_ALLOW_CONTENT)
Test_old_action("Verify deny is reject in modern policy", "", IP_ALLOW_OLD_ACTION)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify non-allowed methods are blocked.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods_pp = Test_remap_acl(
    "Verify non-allowed methods are blocked (PP).",
    replay_file='remap_acl_get_post_allowed_pp.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=True)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify add_allow adds an allowed method.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=add_allow @src_ip=127.0.0.1 @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify add_allow adds allowed methods.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=add_allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify if no ACLs match, ip_allow.yaml is used.",
    replay_file='remap_acl_get_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 403, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify @src_ip=all works.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=all @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify @src_ip_category works.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip_category=ACME_LOCAL @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify no @src_ip implies all IP addresses.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify denied methods are blocked.",
    replay_file='remap_acl_get_post_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_deny @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[403, 403, 200, 200, 400],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify add_deny adds blocked methods.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=add_deny @src_ip=127.0.0.1 @method=GET',
    named_acls=[],
    expected_responses=[403, 403, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify a default deny filter rule works.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[403, 403, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip works.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=~127.0.0.1 @method=GET @method=POST',
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[403, 403, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip works with the rule matching.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=~3.4.5.6 @method=GET @method=POST',
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip_category works.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip_category=~ACME_LOCAL @method=GET @method=POST',
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[403, 403, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify inverting @src_ip_category works with the rule matching.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip_category=~ACME_EXTERNAL @method=GET @method=POST',
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify @src_ip and @src_ip_category AND together.",
    replay_file='remap_acl_all_denied.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    # The rule will not match because, while @src_ip matches, @src_ip_category does not.
    acl_configuration='@action=set_allow @src_ip=127.0.0.1 @src_ip_category=ACME_EXTERNAL @method=GET @method=POST',
    # Therefore, this named deny filter will block.
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[403, 403, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify defined in-line ACLS are evaluated before named ones.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[('deny', '@action=set_deny')],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify remap.config line overrides ip_allow rule.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @src_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify we can deactivate the ip_allow filter.",
    replay_file='remap_acl_all_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=True,
    acl_behavior_policy=1,
    # This won't match, so nothing will match since ip_allow.yaml is off.
    acl_configuration='@action=set_allow @src_ip=1.2.3.4 @method=GET @method=POST',
    named_acls=[],
    # Nothing will block the request since ip_allow.yaml is off.
    expected_responses=[200, 200, 200, 200, 400],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify in_ip matches on IP as expected.",
    replay_file='remap_acl_get_post_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @in_ip=127.0.0.1 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 200, 403, 403, 403],
    proxy_protocol=False)

test_ip_allow_optional_methods = Test_remap_acl(
    "Verify in_ip rules do not match on other IPs.",
    replay_file='remap_acl_get_allowed.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='@action=set_allow @in_ip=3.4.5.6 @method=GET @method=POST',
    named_acls=[],
    expected_responses=[200, 403, 403, 403, 403],
    proxy_protocol=False)

test_named_acl_deny = Test_remap_acl(
    "Verify a named ACL is applied if an in-line ACL is absent.",
    replay_file='deny_head_post.replay.yaml',
    ip_allow_content=IP_ALLOW_CONTENT,
    deactivate_ip_allow=False,
    acl_behavior_policy=1,
    acl_configuration='',
    named_acls=[('deny', '@action=set_deny @method=HEAD @method=POST')],
    expected_responses=[200, 403, 403, 403],
    proxy_protocol=False)


def replay_proxy_response(filename, replay_file, get_proxy_response, post_proxy_response):
    """
    replay_proxy_response writes the given replay file (which expects a single GET & POST client-request)
    with the given proxy_response value. This is only used to support the tests in the combination table.
    """

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


from deactivate_ip_allow import all_deactivate_ip_allow_tests
from all_acl_combinations import all_acl_combination_tests
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
    Test_remap_acl(
        "allcombo-{0} {1} {2} {3}".format(idx, test["inline"], test["named_acl"], test["ip_allow"]),
        replay_file=replay_file_name,
        ip_allow_content=test["ip_allow"],
        deactivate_ip_allow=False,
        acl_behavior_policy=0 if test["policy"] == "legacy" else 1,
        acl_configuration=test["inline"],
        named_acls=[("acl", test["named_acl"])] if test["named_acl"] != "" else [],
        expected_responses=[test["GET response"], test["POST response"]],
        proxy_protocol=False,
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
    Test_remap_acl(
        "ipallow-{0} {1} {2} {3}".format(idx, test["inline"], test["named_acl"], test["ip_allow"]),
        replay_file=replay_file_name,
        ip_allow_content=test["ip_allow"],
        deactivate_ip_allow=test["deactivate_ip_allow"],
        acl_behavior_policy=0 if test["policy"] == "legacy" else 1,
        acl_configuration=test["inline"],
        named_acls=[("acl", test["named_acl"])] if test["named_acl"] != "" else [],
        expected_responses=[test["GET response"], test["POST response"]],
        proxy_protocol=False,
    )
