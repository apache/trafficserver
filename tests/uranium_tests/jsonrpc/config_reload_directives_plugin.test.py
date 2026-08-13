'''
Test TSCfgLoadCtxGetReloadDirectives plugin API — verifies that _reload
directives are correctly delivered to the plugin handler, separate from
the supplied config YAML content.

Test scenarios:
  A. Plugin startup: register succeeds
  B. RPC reload with _reload directives (version) + config content (greeting)
  C. File-based reload (touch config) — no directives
  D. RPC reload with empty _reload directives — no version key
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

Test.Summary = 'Test TSCfgLoadCtxGetReloadDirectives plugin API'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess('ts', dump_runroot=True)

Test.testName = 'config_reload_directives_plugin'

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rpc|config|config.reload|cfg_plugin_directives_test',
    })

# Write initial valid plugin config file
ts.Disk.MakeConfigFile('cfg_plugin_directives_test.conf').AddLines([
    'initial: config',
])

# Load the directives test plugin
Test.PrepareTestPlugin(
    os.path.join(Test.Variables.AtsBuildGoldTestsDir, 'jsonrpc', 'plugins', '.libs', 'cfg_plugin_directives_test.so'), ts,
    'cfg_plugin_directives_test.conf')

# ============================================================================
# Test A: Plugin startup — TSCfgRegister succeeds
# ============================================================================
tr = Test.AddTestRun("Plugin loads and registers")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Init-API confirmation is emitted via Dbg() under the cfg_plugin_directives_test tag,
# so it lands in traffic.out, not diags.log.
ts.Disk.traffic_out.Content = Testers.IncludesExpression('TSCfgRegister OK', 'TSCfgRegister should succeed')

ts.Disk.diags_log.Content = All(
    Testers.IncludesExpression(r'Config reload \[rpc-with-directives\] completed',
                               'Reload with directives should appear in diags'),)

# ============================================================================
# Test B: RPC reload with _reload directives + config content
# The framework extracts _reload into directives, remaining content becomes
# supplied_yaml.  Plugin should see both directive_version and content_greeting.
# ============================================================================
tr = Test.AddTestRun("RPC reload with _reload directives and config content")
tr.AddJsonRPCClientRequest(
    ts,
    Request.admin_config_reload(
        token='rpc-with-directives',
        configs={'cfg_plugin_directives_test': {
            'greeting': 'hello_directives',
            '_reload': {
                'version': '2.0'
            }
        }}))


def validate_rpc_directives(resp: Response):
    result = resp.result
    errors = result.get('errors', [])
    if errors:
        return (False, f"Should accept RPC content: {errors}")
    return (True, f"RPC with directives accepted: token={result.get('token', '')}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_rpc_directives)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify directives and content in status")
tr.DelayStart = 5
tr.Processes.Default.Command = "traffic_ctl config status -t rpc-with-directives"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('directive_version=2.0', 'Plugin should see version directive'),
    Testers.IncludesExpression('content_greeting=hello_directives', 'Plugin should see greeting in content'),
    Testers.IncludesExpression('success', 'Should complete successfully'),
    Testers.IncludesExpression(r'\[plugin: ', 'Should have plugin tag'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test C: File-based reload — no directives expected
# Touch the config file to trigger a file-based reload.
# ============================================================================
tr = Test.AddTestRun("File-based reload (touch config) — no directives")
tr.DelayStart = 2
touch_cmd = (
    f'touch {ts.Variables.CONFIGDIR}/cfg_plugin_directives_test.conf '
    f'&& traffic_ctl config reload -t file-no-directives')
tr.Processes.Default.Command = touch_cmd
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify file mode — no directives in status")
tr.DelayStart = 8
tr.Processes.Default.Command = "traffic_ctl config status -t file-no-directives"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('no_directives', 'File-based reload should report no directives'),
    Testers.IncludesExpression('file_mode', 'Should show file_mode path'),
    Testers.IncludesExpression('success', 'Should complete successfully'),
)
tr.StillRunningAfter = ts

# ============================================================================
# Test D: RPC reload with empty _reload directives — no version key
# ============================================================================
tr = Test.AddTestRun("RPC reload with empty _reload directives")
tr.DelayStart = 2
tr.AddJsonRPCClientRequest(
    ts,
    Request.admin_config_reload(
        token='rpc-empty-directives', configs={'cfg_plugin_directives_test': {
            'greeting': 'empty_dir',
            '_reload': {}
        }}, force=True))


def validate_empty_directives(resp: Response):
    result = resp.result
    errors = result.get('errors', [])
    if errors:
        return (False, f"Should accept RPC content: {errors}")
    return (True, f"RPC with empty directives accepted: token={result.get('token', '')}")


tr.Processes.Default.Streams.stdout = Testers.CustomJSONRPCResponse(validate_empty_directives)
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify empty directives — version=none")
tr.DelayStart = 5
tr.Processes.Default.Command = "traffic_ctl config status -t rpc-empty-directives"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = All(
    Testers.IncludesExpression('directive_version=none', 'Empty directives should show version=none'),
    Testers.IncludesExpression('content_greeting=empty_dir', 'Should see greeting content'),
    Testers.IncludesExpression('success', 'Should complete successfully'),
)
tr.StillRunningAfter = ts
