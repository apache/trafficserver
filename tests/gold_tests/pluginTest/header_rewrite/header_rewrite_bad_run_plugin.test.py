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
'''
Verify header_rewrite rejects a run-plugin operator whose target plugin fails to
load. The failure must be caught at config load time, not aborted at request time.
'''

Test.Summary = '''
header_rewrite must reject a run-plugin whose target plugin fails to load, at
config load time, rather than aborting the server on the first request.
'''

# Reproduce the reported crash: run-plugin against a plugin whose instance-init fails
# (conf_remap + a missing file) hands header_rewrite a null instance, which old code aborted on.
Test.SkipUnless(
    Condition.PluginExists('header_rewrite.so'),
    Condition.PluginExists('conf_remap.so'),
)


class TestBadRunPlugin:
    '''Verify failed run-plugin initialization is rejected safely.'''

    ERROR_MARKER: str = 'run-plugin unable to load'
    BAD_RULE_LINES: list[str] = [
        'cond %{REMAP_PSEUDO_HOOK}',
        '  run-plugin conf_remap.so no_such_conf_remap_file.yaml',
    ]
    NESTED_BAD_RULE_LINES: list[str] = [
        'cond %{REMAP_PSEUDO_HOOK}',
        '  if',
        '    cond %{TRUE}',
        '      run-plugin conf_remap.so no_such_conf_remap_file.yaml',
        '  endif',
    ]

    def __init__(self) -> None:
        '''Configure startup and reload rejection scenarios.'''
        self._configure_startup_rejection()
        self._server = self._configure_origin_server()
        self._ts = self._configure_traffic_server()
        self._configure_baseline_request()
        self._configure_bad_remap_install()
        self._configure_failed_reload()
        self._configure_post_reload_request()
        self._ts.Disk.diags_log.Content = Testers.IncludesExpression(
            self.ERROR_MARKER, 'the rejected reload should log the run-plugin failure')

    def _configure_startup_rejection(self) -> None:
        '''Verify a bad top-level run-plugin fails startup cleanly.'''
        ts = Test.MakeATSProcess("ts-startup", disable_log_checks=True)
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'header_rewrite',
        })
        ts.Disk.MakeConfigFile('bad_run_plugin.conf').AddLines(self.BAD_RULE_LINES)
        ts.Disk.remap_config.AddLine(
            'map http://startup.example.com/ http://127.0.0.1/ '
            '@plugin=header_rewrite.so @pparam=bad_run_plugin.conf')

        # Invalid remap.config triggers a controlled exit rather than SIGABRT.
        ts.ReturnCode = 33
        ts.Ready = 0
        ts.Disk.diags_log.Content = Testers.IncludesExpression(
            self.ERROR_MARKER, 'header_rewrite must report the failed run-plugin load')
        ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
            'Traffic Server is fully initialized', 'ATS must not initialize with a bad run-plugin config')

        tr = Test.AddTestRun("Bad run-plugin config fails startup instead of crashing")
        tr.Processes.Default.Command = 'echo verifying startup rejection'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.StartBefore(ts)

    def _configure_origin_server(self) -> 'Process':
        '''Configure the origin used to verify reload behavior.'''
        server = Test.MakeOriginServer("server")
        request_header = {
            "headers": "GET / HTTP/1.1\r\nHost: reload.example.com\r\n\r\n",
            "timestamp": "1469733493.993",
            "body": ""
        }
        response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
        server.addResponse("sessionfile.log", request_header, response_header)
        return server

    def _configure_traffic_server(self) -> 'Process':
        '''Configure ATS with a valid initial remap table.'''
        ts = Test.MakeATSProcess("ts-reload", disable_log_checks=True)
        ts.Disk.records_config.update({
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'header_rewrite',
        })
        ts.Disk.MakeConfigFile('nested_bad_run_plugin.conf').AddLines(self.NESTED_BAD_RULE_LINES)
        ts.Disk.remap_config.AddLine(f'map http://reload.example.com http://127.0.0.1:{self._server.Variables.Port}')
        return ts

    def _configure_curl_run(self, name: str, expectation: str) -> 'TestRun':
        '''Configure a request that verifies ATS still serves traffic.'''
        tr = Test.AddTestRun(name)
        tr.MakeCurlCommand(
            f'--proxy 127.0.0.1:{self._ts.Variables.port} "http://reload.example.com" '
            '-H "Proxy-Connection: keep-alive" --verbose',
            ts=self._ts)
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Streams.stderr = Testers.IncludesExpression('200 OK', expectation)
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server
        return tr

    def _configure_baseline_request(self) -> None:
        '''Verify the valid initial configuration serves requests.'''
        tr = self._configure_curl_run("Baseline request is served before reload", 'baseline request should be served')
        tr.Processes.Default.StartBefore(self._server)
        tr.Processes.Default.StartBefore(self._ts)

    def _configure_bad_remap_install(self) -> None:
        '''Replace remap.config with one containing a bad nested run-plugin.'''
        tr = Test.AddTestRun("Install a remap.config with a bad run-plugin")
        remap_path = self._ts.Disk.remap_config.AbsPath
        tr.Disk.File(remap_path, id="remap_bad", typename="ats:config")
        tr.Disk.remap_bad.AddLine(
            f'map http://reload.example.com http://127.0.0.1:{self._server.Variables.Port} '
            '@plugin=header_rewrite.so @pparam=nested_bad_run_plugin.conf')
        tr.Processes.Default.Command = 'echo installed bad remap.config'
        tr.Processes.Default.ReturnCode = 0
        tr.Processes.Default.Env = self._ts.Env
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

    def _configure_failed_reload(self) -> None:
        '''Verify the bad remap table is rejected without stopping ATS.'''
        tr = Test.AddConfigReload(
            self._ts, expect="fail", delay_start=2, description="Reload with bad run-plugin must be rejected, not fatal")
        tr.StillRunningAfter = self._ts
        tr.StillRunningAfter = self._server

    def _configure_post_reload_request(self) -> None:
        '''Verify the rejected reload leaves the old configuration active.'''
        self._configure_curl_run(
            "Server still serves the old config after the rejected reload", 'old config should still serve after a rejected reload')


TestBadRunPlugin()
