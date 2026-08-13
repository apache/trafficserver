'''
Test deferred TSCfgLoadCtx completion as part of a full config reload.

The deferred plugin uses a two-stage schedule:
  Stage 0: fires 3s after handler returns (simulates "reschedule later")
  Stage 1: fires 2s after stage 0 (simulates "heavy work")
  Total deferred time: ~5 seconds

All tests touch config files to trigger a real full reload so every
registered config reloads alongside the deferred plugin.

Scenarios:
  A. Plugin registers successfully
  B. Full reload — deferred success: in-progress immediately, success after ~5s
  C. Full reload — deferred fail: rewrite config with "defer_fail", verify fail
  D. Verify core tasks complete while plugin is still deferred
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
from jsonrpc import Request, Response

Test.Summary = 'Test deferred two-stage TSCfgLoadCtx completion in full reload'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts', dump_runroot=True, enable_tls=True)

Test.testName = 'config_reload_deferred'

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rpc|config|config.reload|cfg_plugin_deferred_test',
    })

# Write initial valid plugin config file (no "defer_fail" -> success path)
ts.Disk.MakeConfigFile('cfg_plugin_deferred_test.conf').AddLines([
    'mode: success',
])

# Load the deferred test plugin
Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'jsonrpc', 'plugins', '.libs', 'cfg_plugin_deferred_test.so'), ts,
    'cfg_plugin_deferred_test.conf')

# ============================================================================
# Test A: Plugin startup — TSCfgRegister
# ============================================================================
tr = Test.AddTestRun("Plugin loads and registers")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Init-API confirmation is emitted via Dbg() under the cfg_plugin_deferred_test tag,
# so it lands in traffic.out, not diags.log.
ts.Disk.traffic_out.Content = Testers.IncludesExpression('TSCfgRegister OK', 'TSCfgRegister should succeed')

ts.Disk.diags_log.Content = All(
    Testers.IncludesExpression(
        r'Config reload \[full-deferred-ok\] completed', 'Full reload with deferred success should eventually complete'),
    Testers.IncludesExpression(
        r'Config reload \[full-deferred-fail\] finished with failures',
        'Full reload with deferred fail should eventually report failure'),
)

# ============================================================================
# Test B: Full reload — deferred success
# Touch config files to force them to be detected as changed, then trigger
# a normal reload. Don't touch logging.yaml (not present in sandbox).
# ============================================================================
tr = Test.AddTestRun("Touch config files and trigger full reload (deferred success)")
tr.DelayStart = 2
touch_cmd = (
    f'touch {ts.Variables.CONFIGDIR}/ip_allow.yaml '
    f'{ts.Variables.CONFIGDIR}/sni.yaml '
    f'{ts.Variables.CONFIGDIR}/cfg_plugin_deferred_test.conf '
    f'&& traffic_ctl config reload -t full-deferred-ok')
tr.Processes.Default.Command = touch_cmd
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Query immediately — plugin should be in-progress (core tasks may already be done)
tr = Test.AddTestRun("Verify deferred plugin is in-progress while core tasks complete")
tr.DelayStart = 1
tr.Processes.Default.Command = "traffic_ctl config status -t full-deferred-ok"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('in-progress', 'Overall reload should still be in-progress'),
    Testers.IncludesExpression('cfg_plugin_deferred_test', 'Plugin task should appear'),
    Testers.IncludesExpression('deferring work', 'InProgress message from handler should be visible'),
)
tr.StillRunningAfter = ts

# Wait for full deferred completion (~5s + margin)
tr = Test.AddTestRun("Verify full reload reached success after deferred completion")
tr.DelayStart = 12
tr.Processes.Default.Command = "traffic_ctl config status -t full-deferred-ok"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('success', 'Overall reload should be success'),
    Testers.IncludesExpression('deferred complete after heavy work', 'Plugin deferred Complete message should appear'),
    Testers.IncludesExpression('stage 0', 'Stage 0 log should appear'),
    Testers.IncludesExpression('stage 1', 'Stage 1 log should appear'),
    # Core tasks should also appear as completed in the same reload
    Testers.IncludesExpression('ip_allow', 'Core ip_allow task should appear in full reload'),
    Testers.IncludesExpression('sni', 'Core sni task should appear in full reload'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test C: Full reload — deferred fail
# Rewrite the plugin config file with "defer_fail" so the deferred handler
# reads it and calls Fail.
# ============================================================================
tr = Test.AddTestRun("Rewrite config with defer_fail and trigger full reload")
tr.DelayStart = 2
fail_cmd = (
    f'echo "defer_fail: true" > {ts.Variables.CONFIGDIR}/cfg_plugin_deferred_test.conf '
    f'&& touch {ts.Variables.CONFIGDIR}/ip_allow.yaml '
    f'{ts.Variables.CONFIGDIR}/sni.yaml '
    f'&& traffic_ctl config reload -t full-deferred-fail')
tr.Processes.Default.Command = fail_cmd
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Wait for deferred fail (~5s + margin)
tr = Test.AddTestRun("Verify full reload reached fail after deferred failure")
tr.DelayStart = 12
tr.Processes.Default.Command = "traffic_ctl config status -t full-deferred-fail"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('fail', 'Overall reload should show fail'),
    Testers.IncludesExpression('deferred fail after heavy work', 'Plugin deferred Fail message should appear'),
    Testers.IncludesExpression('heavy work failed', 'Stage 1 fail log should appear'),
    # Core tasks should still have completed successfully despite plugin failure
    Testers.IncludesExpression('ip_allow', 'Core ip_allow should appear even when plugin fails'),
)
tr.StillRunningAfter = ts
