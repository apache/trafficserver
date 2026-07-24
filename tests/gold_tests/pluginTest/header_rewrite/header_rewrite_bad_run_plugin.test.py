'''
Verify header_rewrite rejects a run-plugin operator whose target plugin fails to
load. The failure must be caught at config load time, not aborted at request time.
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
header_rewrite must reject a run-plugin whose target plugin fails to load, at
config load time, rather than aborting the server on the first request.
'''

# Reproduce the reported crash: run-plugin against a plugin whose instance-init fails
# (conf_remap + a missing file) hands header_rewrite a null instance, which old code aborted on.
Test.SkipUnless(
    Condition.PluginExists('header_rewrite.so'),
    Condition.PluginExists('conf_remap.so'),
)

# The unique substring header_rewrite logs when a run-plugin target won't load.
error_marker = 'run-plugin unable to load'
bad_rule_lines = [
    'cond %{REMAP_PSEUDO_HOOK}',
    '  run-plugin conf_remap.so no_such_conf_remap_file.yaml',
]

# ----------------------------------------------------------------------------
# Part 1: A bad run-plugin config must fail startup cleanly (no abort/core).
# ----------------------------------------------------------------------------
# disable_log_checks: we deliberately expect ERROR: lines in diags.log.
ts_start = Test.MakeATSProcess("ts-startup", disable_log_checks=True)
ts_start.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'header_rewrite',
})

ts_start.Disk.MakeConfigFile('bad_run_plugin.conf').AddLines(bad_rule_lines)
ts_start.Disk.remap_config.AddLine(
    'map http://startup.example.com/ http://127.0.0.1/ '
    '@plugin=header_rewrite.so @pparam=bad_run_plugin.conf')

# Invalid remap.config -> Emergency() -> _exit(33): a controlled shutdown, not a SIGABRT crash.
ts_start.ReturnCode = 33
ts_start.Ready = 0  # It never reaches "fully initialized"; don't wait for readiness.
ts_start.Disk.diags_log.Content = Testers.IncludesExpression(error_marker, 'header_rewrite must report the failed run-plugin load')
ts_start.Disk.traffic_out.Content = Testers.ExcludesExpression(
    'Traffic Server is fully initialized', 'ATS must not initialize with a bad run-plugin config')

tr_start = Test.AddTestRun("Bad run-plugin config fails startup instead of crashing")
tr_start.Processes.Default.Command = 'echo verifying startup rejection'
tr_start.Processes.Default.ReturnCode = 0
tr_start.Processes.Default.StartBefore(ts_start)

# ----------------------------------------------------------------------------
# Part 2: A bad run-plugin config on reload must be rejected, leaving the running
# server untouched (the reason throwing here is safe, not fatal).
# ----------------------------------------------------------------------------
ts = Test.MakeATSProcess("ts-reload", disable_log_checks=True)
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: reload.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'header_rewrite',
})

# The bad rule file exists from the start; it is only referenced after the reload.
ts.Disk.MakeConfigFile('bad_run_plugin.conf').AddLines(bad_rule_lines)

# Initial config is valid (plain map, no plugin) so ATS comes up and serves.
ts.Disk.remap_config.AddLine(f'map http://reload.example.com http://127.0.0.1:{server.Variables.Port}')


def curl(tr, description):
    tr.MakeCurlCommand(
        f'--proxy 127.0.0.1:{ts.Variables.port} "http://reload.example.com" -H "Proxy-Connection: keep-alive" --verbose', ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stderr = Testers.IncludesExpression('200 OK', description)
    tr.StillRunningAfter = ts
    tr.StillRunningAfter = server


tr1 = Test.AddTestRun("Baseline request is served before reload")
tr1.Processes.Default.StartBefore(server)
tr1.Processes.Default.StartBefore(ts)
curl(tr1, 'baseline request should be served')

# Overwrite remap.config with a mapping whose header_rewrite rule has a bad run-plugin.
tr2 = Test.AddTestRun("Install a remap.config with a bad run-plugin")
remap_path = ts.Disk.remap_config.AbsPath
tr2.Disk.File(remap_path, id="remap_bad", typename="ats:config")
tr2.Disk.remap_bad.AddLine(
    f'map http://reload.example.com http://127.0.0.1:{server.Variables.Port} '
    '@plugin=header_rewrite.so @pparam=bad_run_plugin.conf')
tr2.Processes.Default.Command = 'echo installed bad remap.config'
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Env = ts.Env
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server

# The reload must fail (traffic_ctl exits 2) and ATS must keep running.
tr_reload = Test.AddConfigReload(
    ts, expect="fail", delay_start=2, description="Reload with bad run-plugin must be rejected, not fatal")
tr_reload.StillRunningAfter = ts
tr_reload.StillRunningAfter = server

# The rejected reload keeps the old table, so requests are still served.
tr3 = Test.AddTestRun("Server still serves the old config after the rejected reload")
curl(tr3, 'old config should still serve after a rejected reload')

ts.Disk.diags_log.Content = Testers.IncludesExpression(error_marker, 'the rejected reload should log the run-plugin failure')
