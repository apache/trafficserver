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
Test ssl_multicert partial_reload: with the knob on, a single bad cert should not
block the reload of all other valid certs.
'''

# ---------------------------------------------------------------------------
# Scenario A - default behavior (partial_reload=0): one bad cert aborts the
# entire reload; old config stays fully in place.
# ---------------------------------------------------------------------------

sni_valid = 'valid.example.com'

ts_strict = Test.MakeATSProcess("ts_strict", enable_tls=True, disable_log_checks=True)
server_strict = Test.MakeOriginServer("server_strict")
request_header = {"headers": f"GET / HTTP/1.1\r\nHost: {sni_valid}\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server_strict.addResponse("sessionlog.json", request_header, response_header)

ts_strict.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_strict.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_strict.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        # partial_reload intentionally left at default (0)
    })

ts_strict.addDefaultSSLFiles()
ts_strict.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_strict.Variables.Port}')

ts_strict.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr_strict_1 = Test.AddTestRun("Strict: initial request succeeds")
tr_strict_1.Processes.Default.StartBefore(Test.Processes.ts_strict)
tr_strict_1.Processes.Default.StartBefore(server_strict)
tr_strict_1.StillRunningAfter = ts_strict
tr_strict_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_strict.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_strict.Variables.ssl_port}",
    ts=ts_strict)
tr_strict_1.Processes.Default.ReturnCode = 0

# Overwrite config with one bad entry (missing file) + keep the good default
tr_strict_update = Test.AddTestRun("Strict: inject bad cert into config")
strict_yaml_path = ts_strict.Disk.ssl_multicert_yaml.AbsPath
tr_strict_update.Disk.File(strict_yaml_path, id="strict_yaml", typename="ats:config")
tr_strict_update.Disk.strict_yaml.AddLines(
    """
ssl_multicert:
  - ssl_cert_name: does_not_exist.pem
    ssl_key_name: does_not_exist.key
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))
tr_strict_update.StillRunningAfter = ts_strict
tr_strict_update.Processes.Default.Command = 'echo Updated strict config'
tr_strict_update.Processes.Default.Env = ts_strict.Env
tr_strict_update.Processes.Default.ReturnCode = 0

# Reload should fail - strict mode rejects the whole config
tr_strict_reload = Test.AddConfigReload(
    ts_strict, expect="fail", expect_tasks=["ssl_multicert.yaml"], description="Strict: reload expected to fail")
tr_strict_reload.StillRunningAfter = server_strict

# Old cert still served because reload was rolled back
tr_strict_2 = Test.AddTestRun("Strict: old cert still served after failed reload")
tr_strict_2.StillRunningAfter = ts_strict
tr_strict_2.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_strict.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_strict.Variables.ssl_port}",
    ts=ts_strict)
tr_strict_2.Processes.Default.ReturnCode = 0
tr_strict_2.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    "CN=example.com", "Old default cert should still be served")

# The counter must be incremented even in strict mode so operators can alert on failures
# regardless of which reload policy is active.
tr_strict_metric = Test.AddTestRun("Strict: ssl_multicert_load_failures counter incremented")
tr_strict_metric.StillRunningAfter = ts_strict
tr_strict_metric.Processes.Default.Command = (
    f"{ts_strict.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_strict_metric.Processes.Default.Env = ts_strict.Env
tr_strict_metric.Processes.Default.ReturnCode = 0
tr_strict_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]",
    "Failure counter must be incremented even when strict mode rolls back the config")

# ---------------------------------------------------------------------------
# Scenario B - partial_reload=1: a bad cert is skipped, valid certs are
# applied. The domain whose cert failed falls back to the default context.
# ---------------------------------------------------------------------------

ts_partial = Test.MakeATSProcess("ts_partial", enable_tls=True, disable_log_checks=True)
server_partial = Test.MakeOriginServer("server_partial")
server_partial.addResponse("sessionlog.json", request_header, response_header)

ts_partial.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_partial.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_partial.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.ssl.server.multicert.partial_reload': 1,
    })

ts_partial.addDefaultSSLFiles()
ts_partial.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_partial.Variables.Port}')

ts_partial.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr_partial_1 = Test.AddTestRun("Partial: initial request succeeds")
tr_partial_1.Processes.Default.StartBefore(Test.Processes.ts_partial)
tr_partial_1.Processes.Default.StartBefore(server_partial)
tr_partial_1.StillRunningAfter = ts_partial
tr_partial_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_partial.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_partial.Variables.ssl_port}",
    ts=ts_partial)
tr_partial_1.Processes.Default.ReturnCode = 0

# Inject a mix: one bad entry, one NEWLY-GENERATED good default (different CN)
# The new default cert has CN=reloaded.example.com so that after the partial reload
# we can verify the new config was committed rather than the old one retained.
tr_partial_update = Test.AddTestRun("Partial: inject bad cert alongside new valid default")
partial_yaml_path = ts_partial.Disk.ssl_multicert_yaml.AbsPath
ssl_dir = ts_partial.Variables.SSLDir

# Generate a new self-signed cert with a distinct CN in the same SSL dir
new_cert = f"{ssl_dir}/newdefault.pem"
new_key = f"{ssl_dir}/newdefault.key"
gen_cmd = (
    f"openssl req -x509 -newkey rsa:2048 "
    f"-keyout {new_key} -out {new_cert} "
    f"-days 365 -nodes -subj '/CN=reloaded.example.com' "
    f"2>/dev/null")

tr_partial_update.Disk.File(partial_yaml_path, id="partial_yaml", typename="ats:config")
tr_partial_update.Disk.partial_yaml.AddLines(
    f"""
ssl_multicert:
  - ssl_cert_name: does_not_exist.pem
    ssl_key_name: does_not_exist.key
  - dest_ip: "*"
    ssl_cert_name: newdefault.pem
    ssl_key_name: newdefault.key
""".split("\n"))
tr_partial_update.StillRunningAfter = ts_partial
tr_partial_update.Processes.Default.Command = f"{gen_cmd} && echo Updated partial config"
tr_partial_update.Processes.Default.Env = ts_partial.Env
tr_partial_update.Processes.Default.ReturnCode = 0

# Reload should succeed despite the bad entry (partial_reload=1)
tr_partial_reload = Test.AddConfigReload(
    ts_partial,
    expect="success",
    expect_tasks=["ssl_multicert.yaml"],
    description="Partial: reload expected to succeed despite bad cert")
tr_partial_reload.StillRunningAfter = server_partial

# Default cert is still reachable and the CN proves the NEW config was committed,
# not the old server.pem (CN=example.com) that was kept.
tr_partial_2 = Test.AddTestRun("Partial: new cert committed and served after partial reload")
tr_partial_2.StillRunningAfter = ts_partial
tr_partial_2.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_partial.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_partial.Variables.ssl_port}",
    ts=ts_partial)
tr_partial_2.Processes.Default.ReturnCode = 0
tr_partial_2.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    "CN=reloaded.example.com", "New default cert (not old server.pem) must be served")

# Match the specific loader error message so the assertion fails if the wrong
# (or no) cert-load error was logged, not merely any unrelated ERROR entry.
# The actual diags.log message comes from SSLError() in SSLUtils.cc:
#   "failed to load certificate secret for <path>/does_not_exist.pem ..."
ts_partial.Disk.diags_log.Content = Testers.IncludesExpression(
    "failed to load certificate secret for.*does_not_exist\\.pem",
    "bad cert should produce a specific cert-load error in diags.log")

# Verify the ssl_multicert_load_failures metric was incremented
tr_metric = Test.AddTestRun("Partial: ssl_multicert_load_failures metric incremented")
tr_metric.StillRunningAfter = ts_partial
tr_metric.Processes.Default.Command = (
    f"{ts_partial.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_metric.Processes.Default.Env = ts_partial.Env
tr_metric.Processes.Default.ReturnCode = 0
tr_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]",
    "Failure counter must be at least 1 after a partial reload with a bad cert")

# ---------------------------------------------------------------------------
# Scenario C - SNI-only deployment (no dest_ip: "*"): partial_reload=1 must
# still commit the good certs even when there is no wildcard default entry.
# This covers CDN-style configurations that rely purely on SNI matching.
# ---------------------------------------------------------------------------

ts_sni_only = Test.MakeATSProcess("ts_sni_only", enable_tls=True, disable_log_checks=True)
server_sni_only = Test.MakeOriginServer("server_sni_only")
server_sni_only.addResponse("sessionlog.json", request_header, response_header)

ts_sni_only.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_sni_only.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_sni_only.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.ssl.server.multicert.partial_reload': 1,
    })

ts_sni_only.addDefaultSSLFiles()
ts_sni_only.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_sni_only.Variables.Port}')
ssl_dir_c = ts_sni_only.Variables.SSLDir

sni_c_new = 'sni-c-new.example.com'

# SNI-only config: server.pem is listed without dest_ip: "*" so it is matched
# by CN/SAN only. ATS will create a bare bootstrap context internally.
ts_sni_only.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr_sni_only_1 = Test.AddTestRun("SNI-only: initial request succeeds")
tr_sni_only_1.Processes.Default.StartBefore(Test.Processes.ts_sni_only)
tr_sni_only_1.Processes.Default.StartBefore(server_sni_only)
tr_sni_only_1.StillRunningAfter = ts_sni_only
tr_sni_only_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_sni_only.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_sni_only.Variables.ssl_port}",
    ts=ts_sni_only)
tr_sni_only_1.Processes.Default.ReturnCode = 0

# Update: inject bad cert + a newly-generated SNI cert with a distinct CN.
# Using a distinct CN (sni-c-new.example.com instead of server.pem's CN=example.com)
# proves the new config was actually committed and not the old one retained.
tr_sni_only_update = Test.AddTestRun("SNI-only: inject bad cert alongside new distinct-CN SNI cert")
sni_yaml_path = ts_sni_only.Disk.ssl_multicert_yaml.AbsPath
new_sni_c_cert = f"{ssl_dir_c}/sni_c_new.pem"
new_sni_c_key = f"{ssl_dir_c}/sni_c_new.key"
gen_sni_c_cmd = (
    f"openssl req -x509 -newkey rsa:2048 "
    f"-keyout {new_sni_c_key} -out {new_sni_c_cert} "
    f"-days 365 -nodes -subj '/CN={sni_c_new}' "
    f"2>/dev/null")
tr_sni_only_update.Disk.File(sni_yaml_path, id="sni_yaml", typename="ats:config")
tr_sni_only_update.Disk.sni_yaml.AddLines(
    """
ssl_multicert:
  - ssl_cert_name: does_not_exist.pem
    ssl_key_name: does_not_exist.key
  - ssl_cert_name: sni_c_new.pem
    ssl_key_name: sni_c_new.key
""".split("\n"))
tr_sni_only_update.StillRunningAfter = ts_sni_only
tr_sni_only_update.Processes.Default.Command = f"{gen_sni_c_cmd} && echo Updated SNI-only config"
tr_sni_only_update.Processes.Default.Env = ts_sni_only.Env
tr_sni_only_update.Processes.Default.ReturnCode = 0

# Reload must succeed: user_cert_count > 0 because sni_c_new.pem was inserted even
# though ssl_default holds only the bare bootstrap context (no X.509 cert).
tr_sni_only_reload = Test.AddConfigReload(
    ts_sni_only,
    expect="success",
    expect_tasks=["ssl_multicert.yaml"],
    description="SNI-only: partial reload succeeds even without dest_ip: '*' entry")
tr_sni_only_reload.StillRunningAfter = server_sni_only

# The new SNI cert must be served, and the distinct CN proves the NEW config
# was committed (not the old server.pem retained).
tr_sni_only_2 = Test.AddTestRun("SNI-only: new distinct-CN cert committed and served after partial reload")
tr_sni_only_2.StillRunningAfter = ts_sni_only
tr_sni_only_2.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_c_new}:{ts_sni_only.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_c_new}:{ts_sni_only.Variables.ssl_port}",
    ts=ts_sni_only)
tr_sni_only_2.Processes.Default.ReturnCode = 0
tr_sni_only_2.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    f"CN={sni_c_new}", "New SNI cert (not old server.pem) must be served, proving commit happened")

tr_sni_only_metric = Test.AddTestRun("SNI-only: ssl_multicert_load_failures metric incremented")
tr_sni_only_metric.StillRunningAfter = ts_sni_only
tr_sni_only_metric.Processes.Default.Command = (
    f"{ts_sni_only.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_sni_only_metric.Processes.Default.Env = ts_sni_only.Env
tr_sni_only_metric.Processes.Default.ReturnCode = 0
tr_sni_only_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]",
    "Failure counter must be incremented for the skipped cert in SNI-only mode")

# ---------------------------------------------------------------------------
# Scenario D - dest_ip: "*" fails but SNI-specific cert succeeds: partial
# commit must still occur because user_cert_count > 0 (the SNI cert was
# inserted).
# ---------------------------------------------------------------------------

ts_d = Test.MakeATSProcess("ts_d", enable_tls=True, disable_log_checks=True)
server_d = Test.MakeOriginServer("server_d")
server_d.addResponse("sessionlog.json", request_header, response_header)

ts_d.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_d.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_d.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.ssl.server.multicert.partial_reload': 1,
    })

ts_d.addDefaultSSLFiles()
ts_d.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_d.Variables.Port}')
ssl_dir_d = ts_d.Variables.SSLDir

# Initial config: both a wildcard default and a SNI-specific cert
ts_d.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
  - ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr_d_1 = Test.AddTestRun("Default-fails: initial request succeeds")
tr_d_1.Processes.Default.StartBefore(Test.Processes.ts_d)
tr_d_1.Processes.Default.StartBefore(server_d)
tr_d_1.StillRunningAfter = ts_d
tr_d_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_d.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_d.Variables.ssl_port}",
    ts=ts_d)
tr_d_1.Processes.Default.ReturnCode = 0

# Update: break the dest_ip: "*" entry, keep the SNI cert valid
# A distinct SNI cert is generated so we can confirm it was committed.
tr_d_update = Test.AddTestRun("Default-fails: break wildcard default, keep SNI cert valid")
d_yaml_path = ts_d.Disk.ssl_multicert_yaml.AbsPath
new_sni_cert_d = f"{ssl_dir_d}/sni_cert_d.pem"
new_sni_key_d = f"{ssl_dir_d}/sni_cert_d.key"
gen_sni_cmd_d = (
    f"openssl req -x509 -newkey rsa:2048 "
    f"-keyout {new_sni_key_d} -out {new_sni_cert_d} "
    f"-days 365 -nodes -subj '/CN=sni-d.example.com' "
    f"2>/dev/null")
tr_d_update.Disk.File(d_yaml_path, id="d_yaml", typename="ats:config")
tr_d_update.Disk.d_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: does_not_exist.pem
    ssl_key_name: does_not_exist.key
  - ssl_cert_name: sni_cert_d.pem
    ssl_key_name: sni_cert_d.key
""".split("\n"))
tr_d_update.StillRunningAfter = ts_d
tr_d_update.Processes.Default.Command = f"{gen_sni_cmd_d} && echo Updated default-fails config"
tr_d_update.Processes.Default.Env = ts_d.Env
tr_d_update.Processes.Default.ReturnCode = 0

# Reload must succeed: sni_cert_d.pem was inserted so user_cert_count > 0.
tr_d_reload = Test.AddConfigReload(
    ts_d,
    expect="success",
    expect_tasks=["ssl_multicert.yaml"],
    description="Default-fails: partial reload succeeds because SNI cert is healthy")
tr_d_reload.StillRunningAfter = server_d

tr_d_metric = Test.AddTestRun("Default-fails: ssl_multicert_load_failures metric incremented")
tr_d_metric.StillRunningAfter = ts_d
tr_d_metric.Processes.Default.Command = (
    f"{ts_d.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_d_metric.Processes.Default.Env = ts_d.Env
tr_d_metric.Processes.Default.ReturnCode = 0
tr_d_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]",
    "Failure counter must be incremented when the wildcard default cert fails")

# Handshake to sni-d.example.com must present CN=sni-d.example.com, confirming
# the SNI cert was actually committed and is being served after the partial reload.
tr_d_2 = Test.AddTestRun("Default-fails: SNI cert committed and served after partial reload")
tr_d_2.StillRunningAfter = ts_d
tr_d_2.MakeCurlCommand(
    f"-q -s -v -k --resolve 'sni-d.example.com:{ts_d.Variables.ssl_port}:127.0.0.1' "
    f"https://sni-d.example.com:{ts_d.Variables.ssl_port}",
    ts=ts_d)
tr_d_2.Processes.Default.ReturnCode = 0
tr_d_2.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    "CN=sni-d.example.com", "SNI cert must be served for the matching hostname after partial reload")

# ---------------------------------------------------------------------------
# Scenario E - all certs fail with partial_reload=1: user_cert_count==0 so
# hasAnyCert is false, partialCommit is false, reload must be reported as
# failed and the old configuration must remain active.
# This locks in the behavior that the comment in SSLConfig.cc documents:
# user_cert_count==0 keeps retStatus false, reload is not silently reported as
# "success" when there is literally nothing new to serve.
# ---------------------------------------------------------------------------

ts_e = Test.MakeATSProcess("ts_e", enable_tls=True, disable_log_checks=True)
server_e = Test.MakeOriginServer("server_e")
server_e.addResponse("sessionlog.json", request_header, response_header)

ts_e.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_e.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_e.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.ssl.server.multicert.partial_reload': 1,
    })

ts_e.addDefaultSSLFiles()
ts_e.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_e.Variables.Port}')

# Initial config: one valid default cert so ATS starts successfully.
ts_e.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr_e_1 = Test.AddTestRun("All-fail: initial request succeeds")
tr_e_1.Processes.Default.StartBefore(Test.Processes.ts_e)
tr_e_1.Processes.Default.StartBefore(server_e)
tr_e_1.StillRunningAfter = ts_e
tr_e_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_e.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_e.Variables.ssl_port}",
    ts=ts_e)
tr_e_1.Processes.Default.ReturnCode = 0

# Replace config with ONLY non-existent certs, every entry will fail, user_cert_count==0.
tr_e_update = Test.AddTestRun("All-fail: replace config with entirely invalid certs")
e_yaml_path = ts_e.Disk.ssl_multicert_yaml.AbsPath
tr_e_update.Disk.File(e_yaml_path, id="e_yaml", typename="ats:config")
tr_e_update.Disk.e_yaml.AddLines(
    """
ssl_multicert:
  - ssl_cert_name: does_not_exist_1.pem
    ssl_key_name: does_not_exist_1.key
  - ssl_cert_name: does_not_exist_2.pem
    ssl_key_name: does_not_exist_2.key
""".split("\n"))
tr_e_update.StillRunningAfter = ts_e
tr_e_update.Processes.Default.Command = 'echo Updated all-fail config'
tr_e_update.Processes.Default.Env = ts_e.Env
tr_e_update.Processes.Default.ReturnCode = 0

# Reload must fail: user_cert_count==0 means hasAnyCert=false, partialCommit=false,
# the lookup is discarded and retStatus stays false.
tr_e_reload = Test.AddConfigReload(
    ts_e,
    expect="fail",
    expect_tasks=["ssl_multicert.yaml"],
    description="All-fail: reload must fail even with partial_reload=1 when user_cert_count==0")
tr_e_reload.StillRunningAfter = server_e

# Old cert must still be served because the reload was rolled back.
tr_e_2 = Test.AddTestRun("All-fail: old cert still served after all-fail reload")
tr_e_2.StillRunningAfter = ts_e
tr_e_2.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_e.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_e.Variables.ssl_port}",
    ts=ts_e)
tr_e_2.Processes.Default.ReturnCode = 0
tr_e_2.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    "CN=example.com", "Old default cert must still be served when all new certs failed")

# Counter must be incremented for each cert that failed, even though nothing was committed.
tr_e_metric = Test.AddTestRun("All-fail: ssl_multicert_load_failures counter incremented")
tr_e_metric.StillRunningAfter = ts_e
tr_e_metric.Processes.Default.Command = (
    f"{ts_e.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_e_metric.Processes.Default.Env = ts_e.Env
tr_e_metric.Processes.Default.ReturnCode = 0
tr_e_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]",
    "Failure counter must be incremented for each cert that could not be loaded")

# ---------------------------------------------------------------------------
# Scenario F - EC certificate (prime256v1): partial_reload=1 with a good EC
# cert alongside a bad RSA entry. On BoringSSL the EC cert lands in ec_storage.
# count(SSLCertContextType::EC) > 0 makes hasAnyCert true so the partial commit
# proceeds. On vanilla OpenSSL, EC certs fall through to ssl_storage, so
# count(SSLCertContextType::RSA) > 0 achieves the same result. This scenario
# exercises the ec_storage code path that all-RSA scenarios leave uncovered.
# ---------------------------------------------------------------------------

ts_f = Test.MakeATSProcess("ts_f", enable_tls=True, disable_log_checks=True)
server_f = Test.MakeOriginServer("server_f")
server_f.addResponse("sessionlog.json", request_header, response_header)

ts_f.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_f.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_f.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.ssl.server.multicert.partial_reload': 1,
    })

ts_f.addDefaultSSLFiles()
ts_f.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_f.Variables.Port}')
ssl_dir_f = ts_f.Variables.SSLDir

sni_ec = 'ec.example.com'

# Initial config: vanilla RSA default so ATS starts cleanly.
ts_f.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
""".split("\n"))

tr_f_1 = Test.AddTestRun("EC: initial request succeeds")
tr_f_1.Processes.Default.StartBefore(Test.Processes.ts_f)
tr_f_1.Processes.Default.StartBefore(server_f)
tr_f_1.StillRunningAfter = ts_f
tr_f_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_valid}:{ts_f.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_valid}:{ts_f.Variables.ssl_port}",
    ts=ts_f)
tr_f_1.Processes.Default.ReturnCode = 0

# Inject: one bad RSA entry + one good EC cert (prime256v1, CN=ec.example.com).
# user_cert_count is incremented for each successfully loaded cert regardless of
# storage type. On BoringSSL the EC cert lands in ec_storage and the RSA cert in
# ssl_storage; on vanilla OpenSSL both land in ssl_storage. Either way,
# user_cert_count > 0 after any cert loads successfully.
tr_f_update = Test.AddTestRun("EC: inject bad RSA cert alongside good EC cert")
f_yaml_path = ts_f.Disk.ssl_multicert_yaml.AbsPath
ec_cert_f = f"{ssl_dir_f}/ecgood.pem"
ec_key_f = f"{ssl_dir_f}/ecgood.key"
sni_rsa_f = 'rsa-f.example.com'
rsa_cert_f = f"{ssl_dir_f}/rsagood.pem"
rsa_key_f = f"{ssl_dir_f}/rsagood.key"
# Generate both EC and RSA certs. On BoringSSL ec_storage and ssl_storage are
# each populated by one cert; on vanilla OpenSSL both land in ssl_storage.
# user_cert_count == 2 after both load, making hasAnyCert true regardless of build.
gen_ec_cmd = (
    f"openssl ecparam -name prime256v1 -genkey -noout -out {ec_key_f} 2>/dev/null && "
    f"openssl req -new -x509 -key {ec_key_f} -out {ec_cert_f} "
    f"-days 365 -nodes -subj '/CN={sni_ec}' 2>/dev/null")
gen_rsa_f_cmd = (
    f"openssl req -x509 -newkey rsa:2048 "
    f"-keyout {rsa_key_f} -out {rsa_cert_f} "
    f"-days 365 -nodes -subj '/CN={sni_rsa_f}' "
    f"2>/dev/null")

tr_f_update.Disk.File(f_yaml_path, id="f_yaml", typename="ats:config")
tr_f_update.Disk.f_yaml.AddLines(
    """
ssl_multicert:
  - ssl_cert_name: does_not_exist.pem
    ssl_key_name: does_not_exist.key
  - ssl_cert_name: ecgood.pem
    ssl_key_name: ecgood.key
  - ssl_cert_name: rsagood.pem
    ssl_key_name: rsagood.key
""".split("\n"))
tr_f_update.StillRunningAfter = ts_f
tr_f_update.Processes.Default.Command = f"{gen_ec_cmd} && {gen_rsa_f_cmd} && echo Updated EC+RSA config"
tr_f_update.Processes.Default.Env = ts_f.Env
tr_f_update.Processes.Default.ReturnCode = 0

# Reload must succeed: user_cert_count==2 (ecgood + rsagood both loaded), so
# hasAnyCert is true and partial commit proceeds.
tr_f_reload = Test.AddConfigReload(
    ts_f,
    expect="success",
    expect_tasks=["ssl_multicert.yaml"],
    description="EC+RSA: partial reload succeeds with both EC and RSA certs committed")
tr_f_reload.StillRunningAfter = server_f

# EC handshake: CN=ec.example.com proves the EC cert was committed and served.
tr_f_2 = Test.AddTestRun("EC+RSA: EC cert committed and served after partial reload")
tr_f_2.StillRunningAfter = ts_f
tr_f_2.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_ec}:{ts_f.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_ec}:{ts_f.Variables.ssl_port}", ts=ts_f)
tr_f_2.Processes.Default.ReturnCode = 0
tr_f_2.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    "CN=ec.example.com", "EC cert must be served after partial reload committed it")

# RSA handshake: CN=rsa-f.example.com proves the RSA cert was committed alongside EC.
# Verifies both EC and RSA certs were committed by the partial reload.
tr_f_3 = Test.AddTestRun("EC+RSA: RSA cert also committed and served after partial reload")
tr_f_3.StillRunningAfter = ts_f
tr_f_3.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_rsa_f}:{ts_f.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_rsa_f}:{ts_f.Variables.ssl_port}",
    ts=ts_f)
tr_f_3.Processes.Default.ReturnCode = 0
tr_f_3.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    f"CN={sni_rsa_f}", "RSA cert must also be served when committed alongside EC cert")

tr_f_metric = Test.AddTestRun("EC+RSA: ssl_multicert_load_failures counter incremented")
tr_f_metric.StillRunningAfter = ts_f
tr_f_metric.Processes.Default.Command = (
    f"{ts_f.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_f_metric.Processes.Default.Env = ts_f.Env
tr_f_metric.Processes.Default.ReturnCode = 0
tr_f_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]", "Failure counter must be incremented for the bad cert that was skipped")

# ---------------------------------------------------------------------------
# Scenario G - previously-good SNI host stops serving its cert after it fails
# on partial reload. After a partial commit, a hostname whose cert failed
# falls back to the bare TLS bootstrap context instead of continuing to serve
# its previously-loaded certificate. This pins the 'stale cert not retained'
# behavior that Bryan's review requested to verify.
# ---------------------------------------------------------------------------

ts_g = Test.MakeATSProcess("ts_g", enable_tls=True, disable_log_checks=True)
server_g = Test.MakeOriginServer("server_g")
server_g.addResponse("sessionlog.json", request_header, response_header)

ts_g.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts_g.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts_g.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.ssl.server.multicert.partial_reload': 1,
    })

ts_g.addDefaultSSLFiles()
ts_g.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server_g.Variables.Port}')
ssl_dir_g = ts_g.Variables.SSLDir

sni_alpha = 'alpha.example.com'
sni_beta = 'beta.example.com'
sni_gamma = 'gamma.example.com'
alpha_cert = f"{ssl_dir_g}/alpha.pem"
alpha_key = f"{ssl_dir_g}/alpha.key"
beta_cert = f"{ssl_dir_g}/beta.pem"
beta_key = f"{ssl_dir_g}/beta.key"

gen_alpha_cmd = (
    f"openssl req -x509 -newkey rsa:2048 -keyout {alpha_key} -out {alpha_cert} "
    f"-days 365 -nodes -subj '/CN={sni_alpha}' 2>/dev/null")
gen_beta_cmd = (
    f"openssl req -x509 -newkey rsa:2048 -keyout {beta_key} -out {beta_cert} "
    f"-days 365 -nodes -subj '/CN={sni_beta}' 2>/dev/null")

# Alpha and beta certs must exist before ATS starts so both are available at
# startup. A dedicated setup step generates them ahead of ts_g.StartBefore.
# mkdir -p is required because the sandbox ssl directory does not yet exist
# at this point: ts_g has not been started and AuTest only creates the process
# directory tree when the process first runs.
tr_g_setup = Test.AddTestRun("G: generate alpha and beta certs before ATS starts")
tr_g_setup.Processes.Default.Command = (f"mkdir -p {ssl_dir_g} && {gen_alpha_cmd} && {gen_beta_cmd} && echo G certs generated")
tr_g_setup.Processes.Default.Env = ts_g.Env
tr_g_setup.Processes.Default.ReturnCode = 0

# Initial config: wildcard default (server.pem) + two distinct SNI certs.
# The wildcard default is included so that after alpha's cert fails reload,
# connections to alpha.example.com still reach ATS (via the default context)
# and we can assert the OLD alpha cert is no longer presented.
ts_g.Disk.ssl_multicert_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
  - ssl_cert_name: alpha.pem
    ssl_key_name: alpha.key
  - ssl_cert_name: beta.pem
    ssl_key_name: beta.key
""".split("\n"))

tr_g_1 = Test.AddTestRun("G: initial alpha request succeeds with CN=alpha.example.com")
tr_g_1.Processes.Default.StartBefore(Test.Processes.ts_g)
tr_g_1.Processes.Default.StartBefore(server_g)
tr_g_1.StillRunningAfter = ts_g
tr_g_1.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_alpha}:{ts_g.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_alpha}:{ts_g.Variables.ssl_port}",
    ts=ts_g)
tr_g_1.Processes.Default.ReturnCode = 0
tr_g_1.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_alpha}", "alpha cert must be served before reload")

tr_g_1b = Test.AddTestRun("G: initial beta request succeeds with CN=beta.example.com")
tr_g_1b.StillRunningAfter = ts_g
tr_g_1b.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_beta}:{ts_g.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_beta}:{ts_g.Variables.ssl_port}",
    ts=ts_g)
tr_g_1b.Processes.Default.ReturnCode = 0
tr_g_1b.Processes.Default.Streams.stderr = Testers.IncludesExpression(f"CN={sni_beta}", "beta cert must be served before reload")

# Reload config: alpha.pem is replaced by a non-existent file (alpha's cert
# fails to load), beta.pem is kept, and gamma.pem is newly added.
gamma_cert = f"{ssl_dir_g}/gamma.pem"
gamma_key = f"{ssl_dir_g}/gamma.key"
gen_gamma_cmd = (
    f"openssl req -x509 -newkey rsa:2048 -keyout {gamma_key} -out {gamma_cert} "
    f"-days 365 -nodes -subj '/CN={sni_gamma}' 2>/dev/null")

tr_g_update = Test.AddTestRun("G: break alpha, keep beta, add gamma")
g_yaml_path = ts_g.Disk.ssl_multicert_yaml.AbsPath
tr_g_update.Disk.File(g_yaml_path, id="g_yaml", typename="ats:config")
tr_g_update.Disk.g_yaml.AddLines(
    """
ssl_multicert:
  - dest_ip: "*"
    ssl_cert_name: server.pem
    ssl_key_name: server.key
  - ssl_cert_name: does_not_exist.pem
    ssl_key_name: does_not_exist.key
  - ssl_cert_name: beta.pem
    ssl_key_name: beta.key
  - ssl_cert_name: gamma.pem
    ssl_key_name: gamma.key
""".split("\n"))
tr_g_update.StillRunningAfter = ts_g
tr_g_update.Processes.Default.Command = f"{gen_gamma_cmd} && echo G config updated"
tr_g_update.Processes.Default.Env = ts_g.Env
tr_g_update.Processes.Default.ReturnCode = 0

# Reload must succeed: server.pem, beta.pem, and gamma.pem all loaded cleanly.
tr_g_reload = Test.AddConfigReload(
    ts_g,
    expect="success",
    expect_tasks=["ssl_multicert.yaml"],
    description="G: partial reload succeeds with server, beta, gamma committed")
tr_g_reload.StillRunningAfter = server_g

# alpha.example.com must no longer present CN=alpha.example.com: that cert was
# not committed in the partial reload. ATS falls back to the wildcard default
# context (server.pem, CN=example.com) for the unmatched SNI.
# This directly pins the 'previously-good SNI stops serving its old cert' behavior.
tr_g_alpha = Test.AddTestRun("G: alpha SNI falls back to default context after its cert fails reload")
tr_g_alpha.StillRunningAfter = ts_g
tr_g_alpha.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_alpha}:{ts_g.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_alpha}:{ts_g.Variables.ssl_port}",
    ts=ts_g)
tr_g_alpha.Processes.Default.ReturnCode = 0
tr_g_alpha.Processes.Default.Streams.stderr = Testers.ExcludesExpression(
    f"CN={sni_alpha}", "alpha.example.com must no longer present the old alpha cert after it failed to reload")

# beta.example.com must still serve its cert unchanged.
tr_g_beta = Test.AddTestRun("G: beta SNI still served with its cert after partial reload")
tr_g_beta.StillRunningAfter = ts_g
tr_g_beta.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_beta}:{ts_g.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_beta}:{ts_g.Variables.ssl_port}",
    ts=ts_g)
tr_g_beta.Processes.Default.ReturnCode = 0
tr_g_beta.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    f"CN={sni_beta}", "beta cert must still be served after partial reload committed it")

# gamma.example.com must now be served: newly committed cert.
tr_g_gamma = Test.AddTestRun("G: gamma SNI newly served after partial reload")
tr_g_gamma.StillRunningAfter = ts_g
tr_g_gamma.MakeCurlCommand(
    f"-q -s -v -k --resolve '{sni_gamma}:{ts_g.Variables.ssl_port}:127.0.0.1' "
    f"https://{sni_gamma}:{ts_g.Variables.ssl_port}",
    ts=ts_g)
tr_g_gamma.Processes.Default.ReturnCode = 0
tr_g_gamma.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    f"CN={sni_gamma}", "gamma cert must be served now that it was committed by partial reload")

tr_g_metric = Test.AddTestRun("G: ssl_multicert_load_failures counter incremented")
tr_g_metric.StillRunningAfter = ts_g
tr_g_metric.Processes.Default.Command = (
    f"{ts_g.Variables.BINDIR}/traffic_ctl metric get"
    f" proxy.process.ssl.ssl_multicert_load_failures")
tr_g_metric.Processes.Default.Env = ts_g.Env
tr_g_metric.Processes.Default.ReturnCode = 0
tr_g_metric.Processes.Default.Streams.stdout = Testers.IncludesExpression(
    "proxy.process.ssl.ssl_multicert_load_failures [1-9]",
    "Failure counter must be incremented for the alpha cert that failed to reload")
