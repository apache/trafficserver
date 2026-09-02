'''
Test that reserve_subtask correctly pre-registers subtasks during a reload.

When records.yaml is changed alongside other config files, the "records"
subtask completes first (records.yaml is always processed first in
rereadConfig).  This pushes the main task to SUCCESS before other
file-based or record-triggered handlers have a chance to register their
subtasks.  reserve_subtask() must accept a SUCCESS parent and pull it back
to IN_PROGRESS by adding a CREATED child.

This test:
  1. Modifies records.yaml on disk (--cold) to change a trigger record so
     that a record-triggered handler fires during the next reload.
  2. Touches additional config files so file-based handlers also fire.
  3. Triggers a reload with a named token.
  4. Verifies that traffic.out contains "Reserved subtask" messages — proving
     reserve_subtask() succeeded despite the parent being SUCCESS.
  5. Verifies no "ignoring transition" warnings (no state conflicts).
  6. Verifies the reload reaches a terminal state with all subtasks tracked.
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

Test.Summary = 'Verify reserve_subtask pre-registers subtasks when records.yaml is first'
Test.ContinueOnFail = True

# --- Setup ---
ts = Test.MakeATSProcess("ts", enable_cache=True)
# Allow extra startup time — cache clearing can take ~10s in some environments.
ts.StartupTimeout = 30
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'rpc|config|config.reload|filemanager',
    })

# Provide valid content for files whose handlers reject empty input.
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
    '    - name: reserve_test',
    '      format: "%<cqtq>"',
])
ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: "*.example.com"',
    '  verify_client: NONE',
])

# Files to touch to trigger file-based handlers during reload.
files_to_touch = [
    ts.Disk.ip_allow_yaml,
    ts.Disk.logging_yaml,
    ts.Disk.sni_yaml,
    ts.Disk.cache_config,
]
touch_cmd = "touch " + " ".join([f.AbsRunTimePath for f in files_to_touch])

# ============================================================================
# Test 1: Start ATS, let it settle, then modify records.yaml on disk
# ============================================================================
tr = Test.AddTestRun("Modify records.yaml via --cold to change a trigger record")
tr.Processes.Default.StartBefore(ts)
# Change debug tags — this touches records.yaml on disk so rereadConfig
# detects it as changed.  The --cold flag modifies the file without
# notifying the running process (the reload will pick it up).
tr.Processes.Default.Command = (
    'sleep 3 && traffic_ctl config set proxy.config.diags.debug.tags '
    '"rpc|config|config.reload|filemanager|upd" --cold')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: Touch additional config files to bump mtime
# ============================================================================
tr = Test.AddTestRun("Touch config files to trigger file-based handlers")
tr.Processes.Default.Command = touch_cmd
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: Trigger reload with a named token
# ============================================================================
tr = Test.AddTestRun("Trigger reload with named token")
tr.Processes.Default.Command = "traffic_ctl config reload -t reserve_subtask_test"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: Wait for all handlers to complete, then query status
# ============================================================================
tr = Test.AddTestRun("Verify reload completed — no tasks stuck in progress")
tr.DelayStart = 15
tr.Processes.Default.Command = "traffic_ctl config status -t reserve_subtask_test"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
# No subtask should still be in_progress after 15s.
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "in_progress", "No task should remain in progress after 15s delay")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("success", "Final reload status must be success")
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: Verify no state-transition conflicts in traffic.out
#
# "ignoring transition from" means two code paths disagree about a task's
# outcome.  This must never happen.
# ============================================================================
ts.Disk.traffic_out.Content += Testers.ExcludesExpression(
    "ignoring transition from", "No state-transition conflicts should appear in traffic.out")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Reserved subtask", "reserve_subtask() must log pre-registration messages")
