'''
Test that a trigger-record value change in records.yaml is handled correctly
during a reload, including deduplication when the 3s config_update_cont timer
fires the same on_record_change callback again.

Scenario:
  1. Change proxy.config.ssl.server.session_ticket.enable in records.yaml
     via --cold.  This is a trigger record for ssl_client_coordinator
     (registered via register_record_config with 11 trigger records).

  2. Touch sni.yaml — this also triggers ssl_client_coordinator via
     add_file_and_node_dependency (proxy.config.ssl.servername.filename).
     So ssl_client_coordinator gets hit from TWO paths: the value change
     callback AND the file-mtime callback.

  3. Touch other config files (ip_allow, logging) for broader coverage.

  4. Trigger a reload and wait for completion.

  5. Wait long enough (>6s) for the 3s config_update_cont timer to fire
     at least once during or after the reload.  When it fires:
       a. If the reload is still in progress — create_config_context()
          finds the existing subtask and logs "Duplicate reload …skipping".
       b. If the reload already completed (SUCCESS) — create_config_context()
          may create a new subtask, the handler runs again, and the task
          re-settles to SUCCESS.
     Either way, no crash, no stuck tasks, no state-transition conflicts.

  6. Verify:
     - "Reserved subtask" messages present (reserve_subtask worked)
     - ssl_client_coordinator subtask completed successfully
     - No "ignoring transition from" messages (no state corruption)
     - Final status is success (even if briefly flickered)
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

Test.Summary = 'Verify trigger-record value change + file-mtime dedup during reload'
Test.ContinueOnFail = True

# --- Setup ---
ts = Test.MakeATSProcess("ts", enable_cache=True)
ts.StartupTimeout = 30

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rpc|config|config.reload|configproc',
    })

# Provide valid content for config files whose handlers reject empty input.
ts.Disk.ip_allow_yaml.AddLines([
    'ip_allow:',
    '- apply: in',
    '  ip_addrs: 0/0',
    '  action: allow',
    '  methods: ALL',
])
ts.Disk.logging_yaml.AddLines([
    'logging:',
    '  formats:',
    '    - name: dedup_test',
    '      format: "%<cqtq>"',
])
ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: "*.example.com"',
    '  verify_client: NONE',
])

# Files to touch — sni.yaml is key because it also triggers
# ssl_client_coordinator via add_file_and_node_dependency.
files_to_touch = [
    ts.Disk.ip_allow_yaml,
    ts.Disk.logging_yaml,
    ts.Disk.sni_yaml,
]
touch_cmd = "touch " + " ".join([f.AbsRunTimePath for f in files_to_touch])

# ============================================================================
# Test 1: Change a trigger-record VALUE in records.yaml via --cold
#
# proxy.config.ssl.server.session_ticket.enable is a trigger record for
# ssl_client_coordinator.  Toggling it from 1→0 modifies records.yaml on
# disk.  When the reload fires, rereadConfig → RecReadYamlConfigFile →
# RecSetRecord sets update_required on this record.  RecFlushConfigUpdateCbs
# fires the on_record_change callback.  Later the 3s timer may fire it again.
# ============================================================================
tr = Test.AddTestRun("Change trigger record value in records.yaml via --cold")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = ('sleep 3 && traffic_ctl config set proxy.config.ssl.server.session_ticket.enable 0 --cold')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: Touch config files — sni.yaml is the critical one
#
# sni.yaml's mtime change triggers RecSetSyncRequired on
# proxy.config.ssl.servername.filename, which is wired to
# ssl_client_coordinator.  So ssl_client_coordinator gets hit from two paths:
#   1. proxy.config.ssl.server.session_ticket.enable value change (from records.yaml)
#   2. proxy.config.ssl.servername.filename sync-required (from sni.yaml mtime)
# This exercises the fan-in deduplication in on_record_change / reserve_subtask.
# ============================================================================
tr = Test.AddTestRun("Touch config files including sni.yaml")
tr.Processes.Default.Command = touch_cmd
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: Trigger reload
# ============================================================================
tr = Test.AddTestRun("Trigger reload with named token")
tr.Processes.Default.Command = "traffic_ctl config reload -t dedup_test"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: Wait long enough for the 3s timer to fire at least twice
#
# The config_update_cont timer fires every 3s.  We wait 15s to ensure:
#   - All subtasks complete (ip_allow, logging, remap, ssl, etc.)
#   - The 3s timer fires at least 4 times during/after the reload
#   - Any duplicate on_record_change callbacks have been processed
# ============================================================================
tr = Test.AddTestRun("Verify reload completed — no tasks stuck in progress")
tr.DelayStart = 15
tr.Processes.Default.Command = "traffic_ctl config status -t dedup_test"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "in_progress", "No task should remain in progress after 15s delay")
# ssl_client_coordinator must be present in the output (it was triggered)
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "ssl_client_coordinator", "ssl_client_coordinator subtask must appear in status")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("success", "Final status must be success")
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: No state-transition conflicts anywhere in the logs
# ============================================================================
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "ignoring transition from", "No state-transition conflicts should appear in traffic.out")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Reserved subtask", "reserve_subtask() must log pre-registration messages")
