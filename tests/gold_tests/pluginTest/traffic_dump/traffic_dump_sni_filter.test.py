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
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\n"
                  "Host: www.example.com\r\nContent-Length: 0\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK"
                   "\r\nConnection: close\r\nContent-Length: 0"
                   "\r\nSet-Cookie: classified_not_for_logging\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

# Define ATS and configure
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'traffic_dump',

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
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: boblite',
    '  verify_client: STRICT',
    '  host_sni_policy: PERMISSIVE',
    '- fqdn: bob',
    '  verify_client: STRICT',
])

# Configure traffic_dump to filter to only dump with connections with SNI bob.
sni_filter = "bob"
ts.Disk.plugin_config.AddLine(
    'traffic_dump.so --logdir {0} --sample 1 --limit 1000000000 '
    '--sni-filter "{1}"'.format(replay_dir, sni_filter)
)

# Set up trafficserver expectations.
ts.Streams.stderr += Testers.ContainsExpression(
    "Filtering to only dump connections with SNI: {}".format(sni_filter),
    "Verify filtering for the expected SNI.")

ts.Streams.stderr += Testers.ContainsExpression(
    "Ignore HTTPS session with non-filtered SNI: dave",
    "Verify that the non-desired SNI session was filtered out.")

# Set up the json replay file expectations.
replay_file_session_1 = os.path.join(replay_dir, "127", "0000000000000000")
ts.Disk.File(replay_file_session_1, exists=True)

# The second session should be filtered out because it doesn't have the
# expected SNI (note exists is set to False).
replay_file_session_2 = os.path.join(replay_dir, "127", "0000000000000001")
ts.Disk.File(replay_file_session_2, exists=False)

# The third session should also be filtered out because it doesn't have any
# SNI (note exists is set to False).
replay_file_session_2 = os.path.join(replay_dir, "127", "0000000000000002")
ts.Disk.File(replay_file_session_2, exists=False)

#
# Test 1: Verify dumping a session with the desired SNI and not dumping
#         the session with the other SNI.
#

# Execute the first transaction with an SNI of bob.
tr = Test.AddTestRun("Verify dumping of a session with the filtered SNI")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = \
    ('curl --http2 --tls-max 1.2 -k -H"Host: bob" --resolve "bob:{0}:127.0.0.1" '
     '--cert ./signed-foo.pem --key ./signed-foo.key --verbose https://bob:{0}'.format(ts.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_sni_bob.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
session_1_protocols = "h2,tls/1.2,tcp,ipv4"
session_1_tls_features = 'sni:bob'

# Execute the second transaction with an SNI of dave.
tr = Test.AddTestRun("Verify that a session of a different SNI is not dumped.")
tr.Processes.Default.Command = \
    ('curl --tls-max 1.2 -k -H"Host: dave" --resolve "dave:{0}:127.0.0.1" '
     '--cert ./signed-foo.pem --key ./signed-foo.key --verbose https://dave:{0}'.format(ts.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_sni_dave.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Execute the third transaction without any SNI.
tr = Test.AddTestRun("Verify that a session of a non-existent SNI is not dumped.")
tr.Processes.Default.Command = \
    ('curl --tls-max 1.2 -k -H"Host: bob"'
     '--cert ./signed-foo.pem --key ./signed-foo.key --verbose https://127.0.0.1:{0}'.format(ts.Variables.ssl_port))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/200_bob_no_sni.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the dumped transaction.
tr = Test.AddTestRun("Verify the json content of the first session")
verify_replay = "verify_replay.py"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = 'python3 {0} {1} {2} --client-protocols "{3}" --client-tls-features "{4}"'.format(
    verify_replay,
    os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json'),
    replay_file_session_1,
    session_1_protocols,
    session_1_tls_features)
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
