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

Test.SkipUnless(Condition.PluginExists('traffic_dump.so'),)

schema_path = os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json')
replay_file = "replay/various_sni.yaml"
server = Test.MakeVerifierServerProcess(
    "server-various-sni", replay_file, ssl_cert="ssl/server_combined.pem", ca_cert="ssl/signer.pem")

# Define ATS and configure
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'traffic_dump',
        'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
        'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
        'proxy.config.url_remap.pristine_host_hdr': 1,
        'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/signer.pem',
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.http.host_sni_policy': 2,
        'proxy.config.ssl.TLSv1_3': 0,
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    })

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.remap_config.AddLine(f'map / https://127.0.0.1:{server.Variables.https_port}')

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bob.com',
    '  verify_client: NONE',
    '  host_sni_policy: PERMISSIVE',
])

# Configure traffic_dump's SNI filter to only dump connections with SNI bob.com.
sni_filter = "bob.com"
ts.Disk.plugin_config.AddLine(f'traffic_dump.so --logdir {replay_dir} --sample 1 '
                              f'--sni-filter "{sni_filter}"')

# Set up trafficserver expectations.
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    f"Filtering to only dump connections with SNI: {sni_filter}", "Verify filtering for the expected SNI.")

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Ignore HTTPS session with non-filtered SNI: dave", "Verify that the non-desired SNI session was filtered out.")

ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Initialized with sample pool size of 1 bytes and unlimited disk utilization",
    "Verify traffic_dump initialized with the configured disk limit.")

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

# Run the traffic with connections containing various SNI values.
tr = Test.AddTestRun("Test SNI filter with various SNI values in the handshakes.")
# Use the same port across the two servers so that the remap config will work
# across both.
server_port = server.Variables.http_port
tr.AddVerifierClientProcess(
    "client-various-sni",
    replay_file,
    https_ports=[ts.Variables.ssl_port],
    ssl_cert="ssl/server_combined.pem",
    ca_cert="ssl/signer.pem")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Verify the properties of the replay file for the dumped transaction.
tr = Test.AddTestRun("Verify the json content of the first session")
session_1_protocols = "http,tls,tcp,ip"
session_1_tls_features = 'sni:bob.com,proxy-verify-mode:0,proxy-provided-cert:true'
verify_replay = "verify_replay.py"
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    (f'{sys.executable} {verify_replay} {schema_path} {replay_file_session_1} '
     f'--client-protocols "{session_1_protocols}" --client-tls-features "{session_1_tls_features}"')
tr.Processes.Default.ReturnCode = 0
