'''
Full reload smoke test.

Verifies that ALL registered config handlers complete properly by:
  Part A: Touching every registered config file, triggering a reload with a
          named token, and verifying all deferred handlers reach a terminal
          state (no in_progress after a delay).
  Part B: Changing one record per module via traffic_ctl config set, waiting,
          then verifying no terminal-state conflicts appear in diags.log.

Registered configs at time of writing:
  Files: ip_allow.yaml, parent.config, cache.config, hosting.config,
         splitdns.config, logging.yaml, sni.yaml, ssl_multicert.config
  Record-only: ssl_ticket_key (proxy.config.ssl.server.ticket_key.filename)

The key assertion is that diags.log does NOT contain:
  "ignoring transition from" — means two code paths disagree about task outcome
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

Test.Summary = 'Full reload smoke test: all config files + record triggers'
Test.ContinueOnFail = True

# --- Setup ---
ts = Test.MakeATSProcess("ts", enable_cache=True)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'rpc|config|reload',
})

# ============================================================================
# Part A: File-based full reload
#
# ATS starts with valid config content (written via AddLines before start).
# We then touch every file to bump mtime and trigger a reload — this exercises
# the full parse path of each handler.
# ============================================================================

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
    '    - name: smoke',
    '      format: "%<cqtq>"',
])
ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: "*.example.com"',
    '  verify_client: NONE',
])
# parent.config, cache.config, hosting.config, splitdns.config,
# ssl_multicert.config are fine empty — handlers accept empty/comment-only files.

# All registered config files whose mtime we'll bump to trigger reload.
files_to_touch = [
    ts.Disk.ip_allow_yaml,
    ts.Disk.parent_config,
    ts.Disk.cache_config,
    ts.Disk.hosting_config,
    ts.Disk.splitdns_config,
    ts.Disk.logging_yaml,
    ts.Disk.sni_yaml,
    ts.Disk.ssl_multicert_config,
]
touch_cmd = "touch " + " ".join([f.AbsRunTimePath for f in files_to_touch])
# Modify records.yaml via traffic_ctl --cold to trigger a real records reload.
records_cmd = 'traffic_ctl config set proxy.config.diags.debug.tags "rpc|config|reload|upd" --cold'

# Test 1: Start ATS, wait for it to settle, update records.yaml on disk
tr = Test.AddTestRun("Update records.yaml via --cold")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"sleep 3 && {records_cmd}"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Test 2: Touch all other config files to bump mtime
tr = Test.AddTestRun("Touch all registered config files")
tr.Processes.Default.Command = touch_cmd
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Test 3: Reload with token — all handlers re-read from disk
tr = Test.AddTestRun("Reload with token - show details")
tr.Processes.Default.Command = "traffic_ctl config reload -t full_reload_smoke"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Test 4: Query status after delay — all deferred handlers (e.g. logging) should have
# reached a terminal state by now.
tr = Test.AddTestRun("Verify no tasks stuck in progress after delay")
tr.DelayStart = 15
tr.Processes.Default.Command = "traffic_ctl config status -t full_reload_smoke"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("in_progress", "No task should remain in progress after 15s")
tr.StillRunningAfter = ts

# ============================================================================
# Part B: Record-triggered reloads (tracing ENABLED — default)
#
# Change one record per module to exercise the RecordTriggeredReloadContinuation path.
# With proxy.config.admin.reload.trace_record_triggers=1 (default), each
# record-triggered reload creates a "rec-" parent task visible in status/history.
# ============================================================================

# One safe record per module:
records_to_change = [
    # (record_name, new_value) — pick values that won't break ATS
    ("proxy.config.log.sampling_frequency", "2"),  # logging
    ("proxy.config.ssl.server.session_ticket.enable", "0"),  # ssl_client_coordinator
]

for record_name, new_value in records_to_change:
    tr = Test.AddTestRun(f"Set {record_name}={new_value}")
    tr.DelayStart = 2
    tr.Processes.Default.Command = f"traffic_ctl config set {record_name} {new_value}"
    tr.Processes.Default.Env = ts.Env
    tr.Processes.Default.ReturnCode = 0
    tr.StillRunningAfter = ts

# Wait for record-triggered reloads to complete
tr = Test.AddTestRun("Wait for record-triggered reloads")
tr.DelayStart = 10
tr.Processes.Default.Command = "echo 'Waiting for record-triggered reloads to settle'"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# Final dump of all reload history
tr = Test.AddTestRun("Fetch all reload history")
tr.Processes.Default.Command = "traffic_ctl config status -c all"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Global assertions on diags.log
# ============================================================================

# No handler should have conflicting terminal state transitions.
# This catches bugs where e.g. evaluate_config() calls fail() and then
# change_configuration() calls complete() — the guard rejects the second call.
ts.Disk.diags_log.Content = Testers.ExcludesExpression("ignoring transition from", "No handler should fight over terminal states")
