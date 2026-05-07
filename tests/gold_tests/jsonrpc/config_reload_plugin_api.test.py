'''
Test the TSCfg* plugin config API end-to-end.

Registration APIs (called in TSPluginInit):
  1. TSCfgRegister          — plugin loads and registers
  2. TSCfgAttachReloadTrigger — record change fires handler
  3. TSCfgAddFileDependency — companion file change fires handler

Handler APIs (called during reload):
  4. TSCfgLoadCtxGetSuppliedYaml — RPC vs file detection
  5. TSCfgLoadCtxGetFilename     — file path resolution
  6. TSCfgLoadCtxInProgress      — in-progress marker (always called)
  7. TSCfgLoadCtxAddLog          — intermediate log entries
  8. TSCfgLoadCtxComplete        — success reporting
  9. TSCfgLoadCtxFail            — failure reporting
  10. TSCfgLoadCtxAddSubtask     — child subtask creation

Test scenarios:
  A. Plugin startup: register + attach trigger + add dependency
  B. RPC reload — success with greeting
  C. RPC reload — fail_on_purpose
  D. RPC reload — with_subtask (parent + child both complete)
  E. RPC reload — subtask_fail (child fails, parent completes)
  F. Record-trigger reload (change proxy.config.http.insert_age_in_response)
  G. Core tasks lack [plugin] tag
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

Test.Summary = 'Test TSCfg* plugin config API: all 10 functions'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts', dump_runroot=True)

Test.testName = 'config_reload_plugin_api'

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rpc|config|config.reload|cfg_plugin_test',
    })

# Write initial valid plugin config file
plugin_config_file = os.path.join(ts.Variables.CONFIGDIR, 'cfg_plugin_test.conf')
ts.Disk.MakeConfigFile('cfg_plugin_test.conf').AddLines([
    'greeting: hello',
])

# Write companion file for TSCfgAddFileDependency
companion_file = os.path.join(ts.Variables.CONFIGDIR, 'cfg_plugin_companion.conf')
ts.Disk.MakeConfigFile('cfg_plugin_companion.conf').AddLines([
    'companion: data',
])

# Load the test plugin with main config + companion file
Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'jsonrpc', 'plugins', '.libs', 'cfg_plugin_test.so'), ts,
    'cfg_plugin_test.conf cfg_plugin_companion.conf')

# ============================================================================
# Test A: Plugin startup — TSCfgRegister + TSCfgAttachReloadTrigger + TSCfgAddFileDependency
# ============================================================================
tr = Test.AddTestRun("Plugin loads and registers all init APIs")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Init-API confirmations are emitted via Dbg() under the cfg_plugin_test tag,
# so they land in traffic.out, not diags.log.
ts.Disk.traffic_out.Content = All(
    Testers.IncludesExpression('TSCfgRegister OK', 'TSCfgRegister should succeed'),
    Testers.IncludesExpression('TSCfgAttachReloadTrigger OK', 'TSCfgAttachReloadTrigger should succeed'),
    Testers.IncludesExpression('TSCfgAddFileDependency OK', 'TSCfgAddFileDependency should succeed'),
)

ts.Disk.diags_log.Content = All(
    # Reload summaries should land in diags.log (escape brackets for regex)
    Testers.IncludesExpression(r'Config reload \[rpc-greet\] completed', 'Reload summary for rpc-greet should appear in diags'),
    Testers.IncludesExpression(
        r'Config reload \[rpc-fail\] finished with failures', 'Reload summary for rpc-fail should report failure in diags'),
    Testers.IncludesExpression(r'Config reload \[rpc-subtask\] completed', 'Reload summary for rpc-subtask should appear in diags'),
    Testers.IncludesExpression(
        r'Config reload \[rpc-subtask-fail\] finished with failures',
        'Reload summary for rpc-subtask-fail should report failure in diags'),
    Testers.IncludesExpression(r'Config reload \[core-check\] completed', 'Reload summary for core-check should appear in diags'),
    Testers.ExcludesExpression('ignoring transition from', 'No terminal state conflicts'),
)

# ============================================================================
# Test B: RPC reload — greet (exercises GetSuppliedYaml, InProgress, AddLog, Complete)
# ============================================================================
tr = Test.AddTestRun("RPC reload with greet key")
tr.AddJsonRPCClientRequest(ts, Request.admin_config_reload(token='rpc-greet', configs={'cfg_plugin_test': {'greet': 'world'}}))


def validate_rpc_greet(resp: Response):
    result = resp.result
    errors = result.get('errors', [])
    if errors:
        return (False, f"Should accept RPC content: {errors}")
    return (True, f"RPC greet accepted: token={result.get('token', '')}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rpc_greet)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify greet reload status")
tr.DelayStart = 5
tr.Processes.Default.Command = "traffic_ctl config status -t rpc-greet"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('[plugin]', 'Plugin task should have [plugin] tag'),
    Testers.IncludesExpression('greet=world', 'Should show greeting in status'),
    Testers.IncludesExpression('success', 'Should show success'),
    Testers.IncludesExpression('handler entered', 'TSCfgLoadCtxAddLog message should appear'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test C: RPC reload — fail_on_purpose (exercises Fail + AddLog)
# ============================================================================
tr = Test.AddTestRun("RPC reload with fail_on_purpose")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts, Request.admin_config_reload(token='rpc-fail', configs={'cfg_plugin_test': {
        'fail_on_purpose': True
    }}, force=True))


def validate_rpc_fail(resp: Response):
    result = resp.result
    errors = result.get('errors', [])
    if errors:
        error_str = str(errors)
        if '6011' in error_str or '6010' in error_str:
            return (False, f"Plugin should accept RPC content: {errors}")
    return (True, f"RPC fail dispatched: token={result.get('token', '')}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rpc_fail)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify fail reload status")
tr.DelayStart = 5
tr.Processes.Default.Command = "traffic_ctl config status -t rpc-fail"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('[plugin]', 'Failed task should have [plugin] tag'),
    Testers.IncludesExpression('fail', 'Should show fail state'),
    Testers.IncludesExpression('fail requested', 'TSCfgLoadCtxAddLog error message should appear'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test D: RPC reload — with_subtask (exercises AddSubtask + InProgress on child)
# ============================================================================
tr = Test.AddTestRun("RPC reload with subtask")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts, Request.admin_config_reload(token='rpc-subtask', configs={'cfg_plugin_test': {
        'with_subtask': True
    }}, force=True))


def validate_rpc_subtask(resp: Response):
    result = resp.result
    return (True, f"RPC subtask dispatched: token={result.get('token', '')}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rpc_subtask)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify subtask in status tree")
tr.DelayStart = 5
tr.Processes.Default.Command = "traffic_ctl config status -t rpc-subtask"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('cfg_plugin_test', 'Parent task should appear'),
    Testers.IncludesExpression('cfg_plugin_test_subtask', 'Child subtask should appear in tree'),
    Testers.IncludesExpression('subtask done', 'Child should show completion message'),
    Testers.IncludesExpression('subtask log entry', 'TSCfgLoadCtxAddLog on child should appear'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test E: RPC reload — subtask_fail (child fails, parent completes)
# ============================================================================
tr = Test.AddTestRun("RPC reload with failing subtask")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts, Request.admin_config_reload(token='rpc-subtask-fail', configs={'cfg_plugin_test': {
        'subtask_fail': True
    }}, force=True))


def validate_rpc_subtask_fail(resp: Response):
    result = resp.result
    return (True, f"RPC subtask-fail dispatched: token={result.get('token', '')}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rpc_subtask_fail)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify failing subtask in status tree")
tr.DelayStart = 5
tr.Processes.Default.Command = "traffic_ctl config status -t rpc-subtask-fail"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('cfg_plugin_test_failing_subtask', 'Failing subtask should appear'),
    Testers.IncludesExpression('subtask failed on purpose', 'Subtask fail message should appear'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test F: Record-trigger reload (TSCfgAttachReloadTrigger)
# Change proxy.config.http.insert_age_in_response to trigger handler
# ============================================================================
tr = Test.AddTestRun("Trigger reload via record change (TSCfgAttachReloadTrigger)")
tr.DelayStart = 2
tr.Processes.Default.Command = "traffic_ctl config set proxy.config.http.insert_age_in_response 0"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Wait for record-triggered reload to complete")
tr.DelayStart = 8
tr.Processes.Default.Command = "traffic_ctl config status -c all"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
# The record-triggered reload should show our plugin task
tr.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    'cfg_plugin_test', 'Plugin handler should have been called by record trigger')
tr.StillRunningAfter = ts

# ============================================================================
# Test G: Core tasks should NOT have [plugin] tag
# ============================================================================
tr = Test.AddTestRun("Full reload for core-check")
tr.DelayStart = 2
tr.Processes.Default.Command = "traffic_ctl config reload -t core-check -F"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify core tasks lack [plugin] tag")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t core-check"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(
    'ip_allow [plugin]', 'Core task ip_allow must not have [plugin] tag')
tr.StillRunningAfter = ts
