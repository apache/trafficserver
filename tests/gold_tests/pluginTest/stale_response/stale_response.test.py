'''
Verify correct stale_response plugin behavior
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

from enum import Enum
import os

Test.Summary = '''
Verify correct stale_response plugin behavior
'''

Test.SkipUnless(Condition.PluginExists('stale_response.so'),)


class OptionType(Enum):
    """Describe what options to pass to the plugin."""

    NONE = 0
    DEFAULT_DIRECTIVES = 1
    FORCE_SWR = 2
    FORCE_SIE = 2


class TestStaleResponse:
    """Verify correct stale_response.so plugin behavior."""

    _replay_file: str
    _ts_counter: int = 0
    _server_counter: int = 0
    _client_counter: int = 0

    def __init__(self, option_type: OptionType, is_global: bool) -> None:
        """Initialize the test.

        :param option_type: The type of options to pass to the stale_response plugin.
        """
        self._option_type = option_type
        self._is_global = is_global

        plugin_type_description = "global" if is_global else "per-remap"
        if option_type == OptionType.NONE:
            self._replay_file = "stale_response_no_default.replay.yaml"
            option_description = f"no stale_response plugin options: {plugin_type_description}"
        elif option_type == OptionType.DEFAULT_DIRECTIVES:
            self._replay_file = "stale_response_with_defaults.replay.yaml"
            option_description = f"--stale-while-revalidate-default 30 --stale-if-error-default 30: {plugin_type_description}"
        elif option_type == OptionType.FORCE_SWR:
            self._replay_file = "stale_response_with_force_swr.replay.yaml"
            option_description = f"--force-stale-while-revalidate 30: {plugin_type_description}"
        elif option_type == OptionType.FORCE_SIE:
            self._replay_file = "stale_response_with_force_sie.replay.yaml"
            option_description = f"--force-stale-if-error 30: {plugin_type_description}"

        tr = Test.AddTestRun(f"stale_response.so Options: {option_description}")

        self.setupOriginServer(tr)
        self.setupTS()
        self.setupClient(tr)
        self.verify_plugin_log()

    def setupOriginServer(self, tr: 'TestRun') -> None:
        """ Configure the server.

        :param tr: The test run to add the server to.
        """
        name = f'server_{TestStaleResponse._server_counter}'
        TestStaleResponse._server_counter += 1
        self._server = tr.AddVerifierServerProcess(name, self._replay_file)

    def setupTS(self) -> None:
        """Configure the traffic server.

        ATS is not configured for a TestRun because we need it to last longer to
        ensure that the plugin's log is created.
        """
        name = f'ts_{TestStaleResponse._ts_counter}'
        TestStaleResponse._ts_counter += 1
        ts = Test.MakeATSProcess(name)
        self._ts = ts

        log_path = os.path.join(ts.Variables.LOGDIR, 'stale_responses.log')
        ts.Disk.File(log_path, id='stale_responses_log')

        remap_plugin_config = ""
        if self._is_global:
            plugin_command = 'stale_response.so --log-all --log-filename stale_responses'
            if self._option_type == OptionType.DEFAULT_DIRECTIVES:
                plugin_command += ' --stale-while-revalidate-default 30 --stale-if-error-default 30'
            elif self._option_type == OptionType.FORCE_SWR:
                plugin_command += ' --force-stale-while-revalidate 30'
            elif self._option_type == OptionType.FORCE_SIE:
                plugin_command += ' --force-stale-if-error 30'
            ts.Disk.plugin_config.AddLine(plugin_command)
        else:
            # Configure the stale_response plugin for the remap rule.
            remap_plugin_config = "@plugin=stale_response.so @pparam=--log-all @pparam=--log-filename @pparam=stale_responses"
            if self._option_type == OptionType.DEFAULT_DIRECTIVES:
                remap_plugin_config += ' @pparam=--stale-while-revalidate-default @pparam=30 @pparam=--stale-if-error-default @pparam=30'
            elif self._option_type == OptionType.FORCE_SWR:
                remap_plugin_config += ' @pparam=--force-stale-while-revalidate @pparam=30'
            elif self._option_type == OptionType.FORCE_SIE:
                remap_plugin_config += ' @pparam=--force-stale-if-error @pparam=30'

        ts.Disk.records_config.update(
            {
                "proxy.config.diags.debug.enabled": 1,
                "proxy.config.diags.debug.tags": "http|stale_response",
                "proxy.config.http.server_session_sharing.pool": "global",
                # Turn off negative revalidating so that we can test stale-if-error.
                "proxy.config.http.negative_revalidating_enabled": 0,
            })
        ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{self._server.Variables.http_port}/ {remap_plugin_config}")

    def setupClient(self, tr: 'TestRun') -> None:

        name = f'client_{TestStaleResponse._client_counter}'
        TestStaleResponse._client_counter += 1
        p = tr.AddVerifierClientProcess(
            name, self._replay_file, http_ports=[self._ts.Variables.port], other_args='--thread-limit 1')
        p.StartBefore(self._server)
        p.StartBefore(self._ts)
        p.StillRunningAfter = self._ts

    def verify_plugin_log(self) -> None:
        """Verify the contents of the stale_response plugin log."""
        tr = Test.AddTestRun("Verify stale_response plugin log")
        name = f'log_waiter_{TestStaleResponse._ts_counter}'
        log_waiter = tr.Processes.Process(name)
        log_waiter.Command = 'sleep 30'
        if self._option_type == OptionType.FORCE_SWR:
            log_waiter.Ready = When.FileContains(self._ts.Disk.stale_responses_log.Name, "stale-while-revalidate:")
            self._ts.Disk.stale_responses_log.Content += Testers.ContainsExpression(
                "stale-while-revalidate:.*stale.jpeg", "Verify stale-while-revalidate directive is logged")
        elif self._option_type == OptionType.FORCE_SIE:
            log_waiter.Ready = When.FileContains(self._ts.Disk.stale_responses_log.Name, "stale-if-error:")
            self._ts.Disk.stale_responses_log.Content += Testers.ContainsExpression(
                "stale-if-error:.*error.jpeg", "Verify stale-if-error directive is logged")
        else:
            log_waiter.Ready = When.FileContains(self._ts.Disk.stale_responses_log.Name, "stale-if-error:")
            self._ts.Disk.stale_responses_log.Content += Testers.ContainsExpression(
                "stale-while-revalidate:.*stale.jpeg", "Verify stale-while-revalidate directive is logged")
            self._ts.Disk.stale_responses_log.Content += Testers.ContainsExpression(
                "stale-if-error:.*error.jpeg", "Verify stale-if-error directive is logged")
        p = tr.Processes.Default
        p.Command = 'echo "Waiting upon the stale response log."'
        p.StartBefore(log_waiter)
        p.StillRunningAfter = self._ts


TestStaleResponse(OptionType.NONE, is_global=True)
TestStaleResponse(OptionType.DEFAULT_DIRECTIVES, is_global=True)
TestStaleResponse(OptionType.FORCE_SWR, is_global=True)
TestStaleResponse(OptionType.FORCE_SIE, is_global=True)

TestStaleResponse(OptionType.NONE, is_global=False)
TestStaleResponse(OptionType.DEFAULT_DIRECTIVES, is_global=False)
TestStaleResponse(OptionType.FORCE_SWR, is_global=False)
TestStaleResponse(OptionType.FORCE_SIE, is_global=False)
