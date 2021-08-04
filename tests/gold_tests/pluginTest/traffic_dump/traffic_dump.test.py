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
Test.Summary = '''
Verify traffic_dump functionality.
'''

Test.SkipUnless(
    Condition.PluginExists('traffic_dump.so'),
)

# Configure the origin server.
replay_file = "replay/traffic_dump.yaml"
server = Test.MakeVerifierServerProcess(
    "server", replay_file,
    ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem")


# Define ATS and configure it.
ts = Test.MakeATSProcess("ts", enable_tls=True)
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Setup.Copy("ssl/signed-foo.pem")
ts.Setup.Copy("ssl/signed-foo.key")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'traffic_dump',
    'proxy.config.http.insert_age_in_response': 0,

    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server': 0,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.http.host_sni_policy': 2,
    'proxy.config.ssl.TLSv1_3': 0,
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map https://www.client_only_tls.com/ http://127.0.0.1:{0}'.format(server.Variables.http_port)
)
ts.Disk.remap_config.AddLine(
    'map https://www.tls.com/ https://127.0.0.1:{0}'.format(server.Variables.https_port)
)
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.http_port)
)

# Configure traffic_dump.
ts.Disk.plugin_config.AddLine(
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 '
    '--sensitive-fields "cookie,set-cookie,x-request-1,x-request-2"'.format(replay_dir)
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
ts.Streams.stderr = Testers.ContainsExpression(
    "Initialized with log directory: {0}".format(replay_dir),
    "Verify traffic_dump initialized with the configured directory.")
ts.Streams.stderr += Testers.ContainsExpression(
    "Initialized with sample pool size 1 bytes and disk limit 1000000000 bytes",
    "Verify traffic_dump initialized with the configured disk limit.")
ts.Streams.stderr += Testers.ContainsExpression(
    "Finish a session with log file of.*bytes",
    "Verify traffic_dump sees the end of sessions and accounts for it.")

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

# Execute the first transaction. We limit the threads to 1 so that the sessions
# are run in serial.
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
    ('python3 {0} {1} {2} {3} --client-http-version "1.1" '
     '--client-protocols "{4}"'.format(
         verify_replay,
         os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
         replay_file_session_1,
         sensitive_fields_arg,
         http_protocols))
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the second transaction.
tr = Test.AddTestRun("Verify the json content of the second session")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    ('python3 {0} {1} {2} {3} --client-http-version "1.1" '
     '--request-target "/two"'.format(
         verify_replay,
         os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
         replay_file_session_2,
         sensitive_fields_arg))
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 2: Verify the correct behavior of an explicit path in the request line.
#

# Verify recording of a request target with the host specified.
tr = Test.AddTestRun("Verify the replay file has the explicit target.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)

tr.Processes.Default.Command = "python3 {0} {1} {2} {3} --request-target '{4}'".format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_3,
    sensitive_fields_arg,
    "http://www.some.host.com/candy")
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
    "python3 {0} {1} {2} {3} --client-request-size {4}".format(
        verify_replay,
        os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
        replay_file_session_4,
        sensitive_fields_arg,
        expected_body_size)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 4: Verify correct handling of a response produced out of the cache.
#
tr = Test.AddTestRun("Verify that the cached response's replay file looks appropriate.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-protocols "{3}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_6,
    http_protocols)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 5: Verify correct handling of two transactions in a session.
#
tr = Test.AddTestRun("Verify the dump file of two transactions in a session.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-protocols "{3}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_7,
    http_protocols)
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
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-protocols "{3}" --client-tls-features "{4}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_8,
    https_protocols,
    client_tls_features)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the server TLS protocol stack.")
https_server_stack = "http,tls,tcp,ip"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
server_tls_features = 'proxy-provided-cert:false,sni:www.tls.com,proxy-verify-mode:1'
tr.Processes.Default.Command = 'python3 {0} {1} {2} --server-protocols "{3}" --server-tls-features "{4}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_8,
    https_server_stack,
    server_tls_features)
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
    ('python3 {0} {1} {2} --client-http-version "2" '
     '--client-protocols "{3}" --client-tls-features "{4}"'.format(
         verify_replay,
         os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
         replay_file_session_9,
         h2_protocols,
         client_tls_features))
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the server HTTP/2 protocol stack.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --server-protocols "{3}" --server-tls-features "{4}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_9,
    https_server_stack,
    server_tls_features)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Test 8: Verify correct protocol dumping of client-side TLS and server-side HTTP.
#
tr = Test.AddTestRun("Verify the client TLS protocol stack.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-http-version "1.1" --client-protocols "{3}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_10,
    https_protocols)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun("Verify the server HTTP protocol stack.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
http_server_stack = "http,tcp,ip"
tr.Processes.Default.Command = 'python3 {0} {1} {2} --server-protocols "{3}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_10,
    http_server_stack)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
