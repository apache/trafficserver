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

import os

Test.Summary = '''
Verify that when runroot is active, path records from records.yaml are
overridden by the resolved Layout paths (precedence: env var > runroot > records.yaml).

When --run-root (or TS_RUNROOT) is set and the PROXY_CONFIG_* environment
variables for path records are unset, RecConfigOverrideFromEnvironment()
returns the actual Layout path (e.g. Layout::bindir, Layout::logdir) which
was populated from runroot.yaml — effectively making runroot.yaml override
records.yaml for these path records.
'''
Test.ContinueOnFail = True
Test.SkipUnless(
    Test.Variables.BINDIR.startswith(Test.Variables.PREFIX), "need to guarantee bin path starts with prefix for runroot")

ts = Test.MakeATSProcess("ts")
ts_dir = os.path.join(Test.RunDirectory, "ts")

# Set deliberately WRONG values in records.yaml for all 4 runroot-managed
# path records.  If runroot override works, these values must NOT be used.
ts.Disk.records_config.append_to_document(
    '''
    bin_path: wrong_bin_path
    local_state_dir: wrong_runtime
    log:
      logfile_dir: wrong_log
    plugin:
      plugin_dir: wrong_plugin
''')

# Test 3 setup: env var that must win over both runroot and records.yaml.
ts.Env['PROXY_CONFIG_DIAGS_DEBUG_TAGS'] = 'env_wins'
ts.Disk.records_config.update('''
    diags:
      debug:
        enabled: 0
        tags: config_value
    ''')

# Build the ATS command:
#   - Unset the 4 path env vars (the test framework always sets them,
#     which masks the runroot code path).
#   - Set TS_RUNROOT to the sandbox dir so the runroot mechanism activates.
original_cmd = ts.Command
ts.Command = (
    "env"
    " -u PROXY_CONFIG_BIN_PATH"
    " -u PROXY_CONFIG_LOCAL_STATE_DIR"
    " -u PROXY_CONFIG_LOG_LOGFILE_DIR"
    " -u PROXY_CONFIG_PLUGIN_PLUGIN_DIR"
    f" TS_RUNROOT={ts_dir}"
    f" {original_cmd}")

# ---------------------------------------------------------------------------
# Test 0: Create runroot.yaml that maps to the sandbox layout, then start ATS.
#
# The runroot.yaml must exist before ATS starts because TS_RUNROOT triggers
# Layout::runroot_setup() during initialization.  We write a runroot.yaml
# whose paths match the sandbox structure the test framework already created
# (traffic_layout init would create a different FHS-style layout that does
# not match the sandbox, so we write it manually).
# ---------------------------------------------------------------------------
runroot_yaml = os.path.join(ts_dir, 'runroot.yaml')

runroot_lines = [
    f"prefix: {ts_dir}",
    f"bindir: {os.path.join(ts_dir, 'bin')}",
    f"sbindir: {os.path.join(ts_dir, 'bin')}",
    f"sysconfdir: {os.path.join(ts_dir, 'config')}",
    f"logdir: {os.path.join(ts_dir, 'log')}",
    f"libexecdir: {os.path.join(ts_dir, 'plugin')}",
    f"localstatedir: {os.path.join(ts_dir, 'runtime')}",
    f"runtimedir: {os.path.join(ts_dir, 'runtime')}",
    f"cachedir: {os.path.join(ts_dir, 'cache')}",
]
runroot_content = "\\n".join(runroot_lines) + "\\n"

tr = Test.AddTestRun("Create runroot.yaml")
tr.Processes.Default.Command = f"mkdir -p {ts_dir} && printf '{runroot_content}' > {runroot_yaml}"
tr.Processes.Default.ReturnCode = 0

# ---------------------------------------------------------------------------
# Test 1: Start ATS with runroot active
# ---------------------------------------------------------------------------
tr = Test.AddTestRun("Start ATS with runroot")
tr.Processes.Default.Command = 'echo start'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = ts

# ATS must not crash (the original nullptr bug) and must complete startup.
ts.Disk.traffic_out.Content = Testers.ExcludesExpression(
    "basic_string", "must not crash with 'basic_string: construction from null'")
ts.Disk.traffic_out.Content += Testers.ContainsExpression("records parsing completed", "ATS should complete records parsing")

# Verify the override log messages appear in traffic.out.
# The errata notes from RecYAMLDecoder are printed by RecCoreInit().
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "'proxy.config.bin_path' overridden with .* by runroot", "bin_path override by runroot must be logged")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "'proxy.config.local_state_dir' overridden with .* by runroot", "local_state_dir override by runroot must be logged")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "'proxy.config.log.logfile_dir' overridden with .* by runroot", "logfile_dir override by runroot must be logged")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "'proxy.config.plugin.plugin_dir' overridden with .* by runroot", "plugin_dir override by runroot must be logged")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "'proxy.config.diags.debug.tags' overridden with 'env_wins' by environment variable",
    "diags.debug.tags override by environment variable must be logged")

# ---------------------------------------------------------------------------
# Test 2: Verify path records do NOT contain the records.yaml values.
#
# Because runroot is active and env vars are unset, the records should hold
# the resolved Layout paths from runroot.yaml, not the records.yaml values.
# ---------------------------------------------------------------------------
tr = Test.AddTestRun("Verify runroot overrides records.yaml for path records")
tr.Processes.Default.Command = (
    'traffic_ctl config get'
    ' proxy.config.bin_path'
    ' proxy.config.local_state_dir'
    ' proxy.config.log.logfile_dir'
    ' proxy.config.plugin.plugin_dir')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# The deliberately wrong records.yaml values must NOT appear.
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression(
    'wrong_bin_path', 'bin_path must be overridden by runroot, not records.yaml')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    'wrong_runtime', 'local_state_dir must be overridden by runroot, not records.yaml')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    'wrong_log', 'logfile_dir must be overridden by runroot, not records.yaml')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    'wrong_plugin', 'plugin_dir must be overridden by runroot, not records.yaml')

# ---------------------------------------------------------------------------
# Test 3: Verify env vars still take highest precedence over runroot.
# ---------------------------------------------------------------------------
tr = Test.AddTestRun("Verify env var overrides both runroot and records.yaml")
tr.Processes.Default.Command = 'traffic_ctl config get proxy.config.diags.debug.tags'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression(
    'proxy.config.diags.debug.tags: env_wins', 'Env var must override both runroot and records.yaml')
