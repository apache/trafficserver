'''
Test SSL coordinator reload state propagation.

Exercises the ssl_client_coordinator tree (SSLConfig, SNIConfig,
SSLCertificateConfig) through success, partial failure (broken sni.yaml),
and recovery.  Verifies:
  - State propagation: child FAIL -> parent FAIL via aggregate_status
  - Severity tags in traffic_ctl output ([Note], [Err])
  - Reload summary in diags.log
  - Debug dump under config.reload tag with severity tags
  - Recovery after fixing broken config
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

Test.Summary = 'Test SSL coordinator reload state propagation and severity tags'
Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts", enable_tls=True, dump_runroot=True)

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'config.reload',
    })

ts.addDefaultSSLFiles()

ts.Disk.ssl_multicert_yaml.AddLines(
    [
        'ssl_multicert:',
        '  - dest_ip: "*"',
        '    ssl_cert_name: server.pem',
        '    ssl_key_name: server.key',
    ])

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: "*.example.com"',
    '  verify_client: NONE',
])

ssl_files_to_touch = [ts.Disk.sni_yaml, ts.Disk.ssl_multicert_yaml]
touch_ssl = "touch " + " ".join([f.AbsRunTimePath for f in ssl_files_to_touch])

# ============================================================================
# Test 1: Start ATS, wait for it to settle
# ============================================================================
tr = Test.AddTestRun("Start ATS")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "sleep 3 && echo 'ATS started'"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: Touch SSL files + trigger baseline reload
# ============================================================================
tr = Test.AddTestRun("Baseline reload - touch SSL files and reload")
tr.Processes.Default.Command = f"{touch_ssl} && traffic_ctl config reload -t ssl-baseline"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: Verify baseline succeeded
# ============================================================================
tr = Test.AddTestRun("Verify baseline - all subtasks success")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-baseline"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("in_progress", "No task should remain in progress")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("FAIL", "Baseline should have no failures")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "\\[Note\\]", "State-transition entries should carry [Note] severity tag")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "ssl_client_coordinator", "ssl_client_coordinator should appear in task tree")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("SSLConfig loading", "SSLConfig should show loading message")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("SSLConfig reloaded", "SSLConfig should show reloaded message")
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: Break sni.yaml and trigger reload
# ============================================================================
sni_path = ts.Disk.sni_yaml.AbsRunTimePath
multicert_path = ts.Disk.ssl_multicert_yaml.AbsRunTimePath
tr = Test.AddTestRun("Break sni.yaml and reload")
tr.Processes.Default.Command = (
    f'printf "sni:\\n- fqdn: example.com\\n  client_cert: /nonexistent/bad.pem\\n  client_key: /nonexistent/bad.key\\n  verify_client: STRICT\\n" > {sni_path}'
    f' && touch {multicert_path}'
    f' && traffic_ctl config reload -t ssl-sni-fail')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: Verify SNI failure propagates to ssl_client_coordinator
# ============================================================================
tr = Test.AddTestRun("Verify SNI failure propagates to coordinator")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-sni-fail"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("FAIL", "Coordinator should propagate FAIL from SNIConfig")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("\\[Err\\]", "SNI failure should carry [Err] severity tag")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "SSLConfig reloaded", "SSLConfig should succeed even when SNI fails")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("sni.yaml failed to load", "SNI failure message should appear")
tr.StillRunningAfter = ts

# ============================================================================
# Test 6: Verify --min-level warning filters out Note entries
# ============================================================================
tr = Test.AddTestRun("Verify --min-level warning filters Note entries")
tr.Processes.Default.Command = ("traffic_ctl config status -t ssl-sni-fail --min-level warning")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "\\[Err\\]", "Error entries should pass --min-level warning filter")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression(
    "\\[Note\\]", "Note entries should be filtered by --min-level warning")
tr.StillRunningAfter = ts

# ============================================================================
# Test 7: Fix sni.yaml and trigger recovery reload
# ============================================================================
tr = Test.AddTestRun("Fix sni.yaml and trigger recovery reload")
tr.Processes.Default.Command = (
    f'printf "sni:\\n- fqdn: \\"*.example.com\\"\\n  verify_client: NONE\\n" > {sni_path}'
    f' && touch {multicert_path}'
    f' && traffic_ctl config reload -t ssl-recovery')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 8: Verify full recovery
# ============================================================================
tr = Test.AddTestRun("Verify full recovery - all subtasks success")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-recovery"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("FAIL", "Recovery reload should have no failures")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("in_progress", "No task should remain in progress after recovery")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "\\[Note\\]", "Recovery entries should carry [Note] severity tags")
tr.StillRunningAfter = ts

# ============================================================================
# Global diags.log assertions
# ============================================================================

# Override default diags check — this test intentionally triggers SSL errors
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "Config reload \\[ssl-baseline\\] completed", "Successful reload should produce a Note summary in diags.log")

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "Config reload \\[ssl-sni-fail\\] finished with failures", "Failed reload should produce a Warning summary in diags.log")

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "Config reload \\[ssl-recovery\\] completed", "Recovery reload should produce a Note summary in diags.log")

ts.Disk.diags_log.Content += Testers.ExcludesExpression(
    "ignoring transition from", "No handler should have conflicting terminal state transitions")
