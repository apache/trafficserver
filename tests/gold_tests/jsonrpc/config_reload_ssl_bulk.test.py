'''
Bulk SSL cert reload with rich status output.

Starts ATS with a single default cert, then on reload pushes 20 certs
via ssl_multicert.yaml.  Exercises all-success and partial-failure
scenarios to show per-cert detail in traffic_ctl and diags.log.
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

Test.Summary = 'Bulk SSL cert reload — 20 certs, partial failure, recovery'
Test.ContinueOnFail = True

NUM_CERTS = 20
BAD_CERTS = {
    5: "garbage",  # garbage PEM content
    9: "empty",  # empty file
    13: "mismatch",  # cert with wrong key
    17: "missing",  # cert file deleted entirely
}

ts = Test.MakeATSProcess("ts", enable_tls=True, dump_runroot=True)
ts.addDefaultSSLFiles()

ssl_dir = ts.Variables.SSLDir
ssl_src_dir = os.path.join(ts.Variables.AtsTestToolsDir, "ssl")

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': ssl_dir,
        'proxy.config.ssl.server.private_key.path': ssl_dir,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'config.reload',
    })

# Startup with just the default cert — simple and reliable
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

ts.Disk.parent_config.AddLine("# empty")
ts.Disk.cache_config.AddLine("# empty")

multicert_path = ts.Disk.ssl_multicert_yaml.AbsRunTimePath
sni_path = ts.Disk.sni_yaml.AbsRunTimePath

# Shell: create 20 cert/key copies + write a new ssl_multicert.yaml with all 20
copy_cmds = " && ".join(
    [
        f"cp {ssl_dir}/server.pem {ssl_dir}/cert-{i:02d}.pem && cp {ssl_dir}/server.key {ssl_dir}/cert-{i:02d}.key"
        for i in range(1, NUM_CERTS + 1)
    ])

# Build the 20-entry ssl_multicert.yaml content
multicert_content = "ssl_multicert:\\n"
for i in range(1, NUM_CERTS + 1):
    multicert_content += f"  - ssl_cert_name: cert-{i:02d}.pem\\n"
    multicert_content += f"    ssl_key_name: cert-{i:02d}.key\\n"

# ============================================================================
# Test 1: Start ATS with default single-cert config
# ============================================================================
tr = Test.AddTestRun("Start ATS with default cert")
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = "sleep 3 && echo ATS started"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 2: Create 20 certs, rewrite ssl_multicert.yaml, trigger reload
# ============================================================================
tr = Test.AddTestRun("Push 20 certs and reload")
tr.Processes.Default.Command = (
    f'{copy_cmds}'
    f' && printf "{multicert_content}" > {multicert_path}'
    f' && touch {sni_path}'
    f' && traffic_ctl config reload -t ssl-bulk-ok')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 3: Verify all 20 certs loaded
# ============================================================================
tr = Test.AddTestRun("Verify all 20 certs loaded")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-bulk-ok"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("FAIL", "Baseline should have no failures")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("ssl_client_coordinator", "Coordinator should appear")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("SSLCertificateConfig", "SSLCertificateConfig should appear")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "ssl_multicert.yaml finished loading", "ssl_multicert.yaml should finish loading")
tr.StillRunningAfter = ts

# ============================================================================
# Test 4: Break 4 certs in different ways, trigger reload
# ============================================================================
break_cmds = [
    f'echo "GARBAGE_NOT_A_CERT" > {ssl_dir}/cert-05.pem',  # garbage content
    f'truncate -s 0 {ssl_dir}/cert-09.pem',  # empty file
    f'cp {ssl_dir}/cert-01.key {ssl_dir}/cert-13.key && touch {ssl_dir}/cert-13.pem',  # key mismatch
    f'rm -f {ssl_dir}/cert-17.pem',  # missing file
]
tr = Test.AddTestRun("Break 4 certs in different ways and reload")
tr.Processes.Default.Command = (
    " && ".join(break_cmds) + f' && touch {multicert_path} {sni_path}' + f' && traffic_ctl config reload -t ssl-bulk-partial')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 5: Verify partial failure — multiple cert errors, rest succeed
# ============================================================================
tr = Test.AddTestRun("Verify partial failure — multiple bad certs")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-bulk-partial"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("FAIL", "Should show failure from bad certs")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("\\[Err\\]", "Error entries should carry [Err] tag")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("cert-05", "Garbage cert-05 should appear in error")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "ssl_multicert.yaml failed to load", "Overall multicert failure should appear")
tr.StillRunningAfter = ts

# ============================================================================
# Test 6: --min-level warning on partial failure
# ============================================================================
tr = Test.AddTestRun("--min-level warning filters bulk Note entries")
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-bulk-partial --min-level warning"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression("\\[Err\\]", "Error entries should pass filter")
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("\\[Note\\]", "Note entries should be filtered out")
tr.StillRunningAfter = ts

# ============================================================================
# Test 7: Fix all broken certs, trigger recovery
# ============================================================================
fix_cmds = [
    f'cp {ssl_dir}/server.pem {ssl_dir}/cert-{i:02d}.pem && cp {ssl_dir}/server.key {ssl_dir}/cert-{i:02d}.key' for i in BAD_CERTS
]
tr = Test.AddTestRun("Fix all broken certs and recover")
tr.Processes.Default.Command = (
    " && ".join(fix_cmds) + f' && touch {multicert_path} {sni_path}' + f' && traffic_ctl config reload -t ssl-bulk-recover')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = ts

# ============================================================================
# Test 8: Verify recovery
# ============================================================================
tr = Test.AddTestRun("Verify recovery — all certs load again")
tr.DelayStart = 10
tr.Processes.Default.Command = "traffic_ctl config status -t ssl-bulk-recover"
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression("FAIL", "Recovery should have no failures")
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    "ssl_multicert.yaml finished loading", "ssl_multicert.yaml should finish loading after recovery")
tr.StillRunningAfter = ts

# ============================================================================
# Global diags.log assertions
# ============================================================================
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "Config reload \\[ssl-bulk-ok\\] completed", "Baseline reload summary in diags.log")

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "Config reload \\[ssl-bulk-partial\\] finished with failures", "Partial failure summary in diags.log")

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "Config reload \\[ssl-bulk-recover\\] completed", "Recovery reload summary in diags.log")

ts.Disk.diags_log.Content += Testers.ExcludesExpression("ignoring transition from", "No conflicting terminal state transitions")
