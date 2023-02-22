"""
Verify traffic_dump functionality.
"""
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
import sys

Test.Summary = '''
Verify traffic_dump functionality.
'''

Test.SkipUnless(
    Condition.PluginExists('traffic_dump.so'),
)

schema_path = os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json')

# Configure the origin server.
replay_file = "replay/traffic_dump.yaml"
server = Test.MakeVerifierServerProcess(
    "server", replay_file,
    ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem")


# Define ATS and configure it.
ts = Test.MakeATSProcess("ts", command='traffic_manager', enable_tls=True)
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Setup.Copy("ssl/signed-foo.pem")
ts.Setup.Copy("ssl/signed-foo.key")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'traffic_dump|http',
    'proxy.config.http.insert_age_in_response': 0,

    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/signer.pem',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.http.host_sni_policy': 2,
    'proxy.config.ssl.TLSv1_3': 0,
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',

    'proxy.config.http.connect_ports': f"{server.Variables.http_port}",
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLines([
    f'map https://www.client_only_tls.com/ http://127.0.0.1:{server.Variables.http_port}',
    f'map https://www.tls.com/ https://127.0.0.1:{server.Variables.https_port}',
    f'map http://www.connect_target.com/ http://127.0.0.1:{server.Variables.http_port}',
    f'map / http://127.0.0.1:{server.Variables.http_port}',
])

# Configure traffic_dump.
ts.Disk.plugin_config.AddLine(
    f'traffic_dump.so --logdir {replay_dir} --sample 1 --limit 1000000000 '
    '--sensitive-fields "cookie,set-cookie,x-request-1,x-request-2"'
)
# Configure logging of transactions. This is helpful for the cache test below.
ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: basic
      format: "%<cluc>: Read result: %<crc>:%<crsc>:%<chm>, Write result: %<cwr>"
  logs:
    - filename: transactions
      format: basic
'''.split('\n'))

# Set up trafficserver expectations.
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    "loading plugin.*traffic_dump.so",
    "Verify the traffic_dump plugin got loaded.")
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    f"Initialized with log directory: {replay_dir}",
    "Verify traffic_dump initialized with the configured directory.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Initialized with sample pool size of 1 bytes and disk limit of 1000000000 bytes",
    "Verify traffic_dump initialized with the configured disk limit.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Finish a session with log file of.*bytes",
    "Verify traffic_dump sees the end of sessions and accounts for it.")
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Dumping body bytes: false",
    "Verify that dumping body bytes is enabled.")

# Set up the json replay file expectations.
replay_file_session_1 = os.path.join(replay_dir, "127", "0000000000000000")
ts.Disk.File(replay_file_session_1, exists=True)
replay_file_session_2 = os.path.join(replay_dir, "127", "0000000000000001")
ts.Disk.File(replay_file_session_2, exists=True)
replay_file_session_3 = os.path.join(replay_dir, "127", "0000000000000002")
ts.Disk.File(replay_file_session_3, exists=True)
replay_file_session_4 = os.path.join(replay_dir, "127", "0000000000000003")
ts.Disk.File(replay_file_session_4, exists=True)
replay_file_session_5 = os.path.join(replay_dir, "127", "0000000000000004")
ts.Disk.File(replay_file_session_5, exists=True)
replay_file_session_6 = os.path.join(replay_dir, "127", "0000000000000005")
ts.Disk.File(replay_file_session_6, exists=True)
replay_file_session_7 = os.path.join(replay_dir, "127", "0000000000000006")
ts.Disk.File(replay_file_session_7, exists=True)
replay_file_session_8 = os.path.join(replay_dir, "127", "0000000000000007")
ts.Disk.File(replay_file_session_8, exists=True)
replay_file_session_9 = os.path.join(replay_dir, "127", "0000000000000008")
ts.Disk.File(replay_file_session_9, exists=True)
replay_file_session_10 = os.path.join(replay_dir, "127", "0000000000000009")
ts.Disk.File(replay_file_session_10, exists=True)
replay_file_session_11 = os.path.join(replay_dir, "127", "000000000000000a")
ts.Disk.File(replay_file_session_11, exists=True)

# The following will not be written to disk because of a restricted disk limit.
replay_file_session_12 = os.path.join(replay_dir, "127", "000000000000000b")
ts.Disk.File(replay_file_session_12, exists=False)

# The following will be written to disk because the disk restriction will be
# removed.
replay_file_session_13 = os.path.join(replay_dir, "127", "000000000000000c")
ts.Disk.File(replay_file_session_13, exists=True)

# The following will not be written to disk because the restriction will be
# re-added.
replay_file_session_14 = os.path.join(replay_dir, "127", "000000000000000d")
ts.Disk.File(replay_file_session_14, exists=False)

# Run our test traffic.
tr = Test.AddTestRun("Run the test traffic.")
tr.AddVerifierClientProcess(
    "client", replay_file, http_ports=[ts.Variables.port],
    https_ports=[ts.Variables.ssl_port],
    ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem",
    other_args='--thread-limit 1')

tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 1: Verify the correct behavior of two transactions across two sessions.
#

# Verify the properties of the replay file for the first transaction.
tr = Test.AddTestRun("Verify the json content of the first session")
http_protocols = "tcp,ip"
verify_replay = "verify_replay.py"
sensitive_fields_arg = (
    "--sensitive-fields cookie "
    "--sensitive-fields set-cookie "
    "--sensitive-fields x-request-1 "
    "--sensitive-fields x-request-2 ")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_1} '
     f'{sensitive_fields_arg} --client-http-version "1.1" '
     f'--client-protocols "{http_protocols}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the second transaction.
tr = Test.AddTestRun("Verify the json content of the second session")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_2} '
     f'{sensitive_fields_arg} --client-http-version "1.1" '
     '--request-target "/two"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 2: Verify the correct behavior of an explicit path in the request line.
#

# Verify recording of a request target with the host specified.
tr = Test.AddTestRun("Verify the replay file has the explicit target.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)

tr.Processes.Default.Command = \
    (f"{sys.executable} {verify_replay} {schema_path} {replay_file_session_3} {sensitive_fields_arg} "
     "--request-target 'http://www.some.host.com/candy'")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 3: Verify correct handling of a POST with body data.
#

tr = Test.AddTestRun("Verify the client-request size node for a request with a body.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)

size_of_verify_replay_file = os.path.getsize(os.path.join(Test.TestDirectory, verify_replay))
expected_body_size = 12345
tr.Processes.Default.Command = \
    (f"{sys.executable} {verify_replay} {schema_path} {replay_file_session_4} {sensitive_fields_arg} "
     f"--client-request-size {expected_body_size}")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 4: Verify correct handling of a response produced out of the cache.
#
tr = Test.AddTestRun("Verify that the cached response's replay file looks appropriate.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_6} '
     f'--client-protocols "{http_protocols}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 5: Verify correct handling of two transactions in a session.
#
tr = Test.AddTestRun("Verify the dump file of two transactions in a session.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_7} '
     f'--client-protocols "{http_protocols}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 6: Verify correct protocol dumping of a TLS connection.
#
tr = Test.AddTestRun("Verify the client protocol stack of a TLS session.")
https_protocols = "tls,tcp,ip"
client_tls_features = "sni:www.tls.com,proxy-verify-mode:0,proxy-provided-cert:true"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_8} '
     f'--client-protocols "{https_protocols}" --client-tls-features "{client_tls_features}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the server TLS protocol stack.")
https_server_stack = "http,tls,tcp,ip"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
server_tls_features = 'proxy-provided-cert:false,sni:www.tls.com,proxy-verify-mode:1'
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_8} --server-protocols '
     f'"{https_server_stack}" --server-tls-features "{server_tls_features}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 7: Verify correct protocol dumping of TLS and HTTP/2 connections.
#
tr = Test.AddTestRun("Verify the client HTTP/2 protocol stack.")
h2_protocols = "http,tls,tcp,ip"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_9} '
     f'--client-http-version "2" --client-protocols "{h2_protocols}" '
     f'--client-tls-features "{client_tls_features}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the server HTTP/2 protocol stack.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_9} '
     f'--server-protocols "{https_server_stack}" --server-tls-features "{server_tls_features}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 8: Verify correct protocol dumping of client-side TLS and server-side HTTP.
#
tr = Test.AddTestRun("Verify the client TLS protocol stack.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_10} '
     f'--client-http-version "1.1" --client-protocols "{https_protocols}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the server HTTP protocol stack.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
http_server_stack = "http,tcp,ip"
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_10} '
     f'--server-protocols "{http_server_stack}"')
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 9: Verify correct handling of a CONNECT request.
#

tr = Test.AddTestRun("Verify handling of a CONNECT request.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)

tr.Processes.Default.Command = \
    (f"{sys.executable} {verify_replay} {schema_path} {replay_file_session_11} {sensitive_fields_arg} ")
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 10: Verify that we can change the --limit value.
#
tr = Test.AddTestRun("Verify changing --limit via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg traffic_dump.limit 0"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Give ATS some time to process the change
tr = Test.AddTestRun("Give ATS some time to process the change")
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Run some more test traffic with the restricted disk limit.")
tr.AddVerifierClientProcess(
    "client-2", replay_file, http_ports=[ts.Variables.port],
    https_ports=[ts.Variables.ssl_port],
    ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem",
    other_args='--keys 1')

# Since the limit is zero, we should not see any new replay file created.
tr = Test.AddTestRun("Verify no new traffic was dumped")
# Sleep 2 seconds to give the replay plugin plenty of time to write the file.
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0
file = tr.Disk.File(replay_file_session_12)
file.Exists = False

#
# Test 11: Verify that we can remove the disk limit.
#
tr = Test.AddTestRun("Removing the disk limit via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg traffic_dump.unlimit"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Give ATS some time to process the change
tr = Test.AddTestRun("Give ATS some time to process the change")
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Run some more test traffic with no disk limit.")
tr.AddVerifierClientProcess(
    "client-3", replay_file, http_ports=[ts.Variables.port],
    https_ports=[ts.Variables.ssl_port],
    ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem",
    other_args='--keys 1')

# Since the limit is zero, we should not see any new replay file created.
tr = Test.AddTestRun("Verify the new traffic was dumped")
# Sleep 2 seconds to give the replay plugin plenty of time to write the file.
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0
file = tr.Disk.File(replay_file_session_13)
file.Exists = True

#
# Test 11: Verify that we can again restrict the disk limit.
#
# Verify that the restriction can be re-added after unlimit was set. This
# verifies correct handling of the boolean controlling unlimited disk space.
#
tr = Test.AddTestRun("Verify re-adding --limit via traffic_ctl.")
tr.Processes.Default.Command = "traffic_ctl plugin msg traffic_dump.limit 0"
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Env = ts.Env
tr.StillRunningAfter = ts

# Give ATS some time to process the change
tr = Test.AddTestRun("Give ATS some time to process the change")
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Run test traffic with newly restricted disk limit.")
tr.AddVerifierClientProcess(
    "client-4", replay_file, http_ports=[ts.Variables.port],
    https_ports=[ts.Variables.ssl_port],
    ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem",
    other_args='--keys 1')

# Since the limit is zero, we should not see any new replay file created.
tr = Test.AddTestRun("Verify no new traffic was dumped")
# Sleep 2 seconds to give the replay plugin plenty of time to write the file.
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0
file = tr.Disk.File(replay_file_session_14)
file.Exists = False
