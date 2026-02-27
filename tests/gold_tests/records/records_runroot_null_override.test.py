'''
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
Verify that records.yaml parsing does not crash when runroot is active and
path records are present without corresponding environment variables.

Additionally, validate that:
1. Env var overrides work correctly (record value comes from env).
2. Runroot-managed path records use config file values when their env vars
   are unset (the bug scenario).
'''

ts = Test.MakeATSProcess("ts")

# Set an env var override for debug tags. This env var is NOT set by the
# test framework, so it specifically tests the env override path in
# RecConfigOverrideFromEnvironment().
ts.Env['PROXY_CONFIG_DIAGS_DEBUG_TAGS'] = 'from_env_override'

# Set a config value for tags that will be overridden by the env var above.
ts.Disk.records_config.update('''
    diags:
      debug:
        enabled: 0
        tags: from_config_value
    ''')

# Add the runroot-managed path records. Values must match the sandbox layout.
ts.Disk.records_config.append_to_document(
    '''
    bin_path: bin
    local_state_dir: runtime
    log:
      logfile_dir: log
    plugin:
      plugin_dir: plugin
''')

# Unset the 4 path env vars to exercise the runroot code path in
# RecConfigOverrideFromEnvironment(). In real deployments (e.g.
# traffic_server --run-root=/path), these env vars are NOT set.
# The test framework always sets them, which masks the bug.
original_cmd = ts.Command
ts.Command = (
    "env"
    " -u PROXY_CONFIG_BIN_PATH"
    " -u PROXY_CONFIG_LOCAL_STATE_DIR"
    " -u PROXY_CONFIG_LOG_LOGFILE_DIR"
    " -u PROXY_CONFIG_PLUGIN_PLUGIN_DIR"
    f" {original_cmd}")

# Test 0: Start ATS
tr = Test.AddTestRun("Start ATS and verify records parsing")
tr.Processes.Default.Command = 'echo 1'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# Verify no basic_string crash.
ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "basic_string", "records.yaml parsing must not crash with 'basic_string: construction from null'")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "records parsing completed", "ATS should complete records parsing without errors")

# Test 1: Verify env var override works for diags.debug.tags
tr = Test.AddTestRun("Verify env var override for diags.debug.tags")
tr.Processes.Default.Command = 'traffic_ctl config get proxy.config.diags.debug.tags'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: from_env_override', 'Record should have the env var override value, not the config file value')

# Test 2: Verify runroot-managed path records have their config file values
tr = Test.AddTestRun("Verify path records have config file values")
tr.Processes.Default.Command = (
    'traffic_ctl config get'
    ' proxy.config.bin_path'
    ' proxy.config.local_state_dir'
    ' proxy.config.log.logfile_dir'
    ' proxy.config.plugin.plugin_dir')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.bin_path: bin', 'bin_path should have the config file value')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.local_state_dir: runtime', 'local_state_dir should have the config file value')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.log.logfile_dir: log', 'logfile_dir should have the config file value')
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'proxy.config.plugin.plugin_dir: plugin', 'plugin_dir should have the config file value')
