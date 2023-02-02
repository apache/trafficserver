"""
Verify traffic_dump response body functionality.
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
Verify traffic_dump response body functionality.
'''

Test.SkipUnless(
    Condition.PluginExists('traffic_dump.so'),
)

# Configure the origin server.
replay_file = "replay/response_body.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)

# Configure ATS.
ts = Test.MakeATSProcess("ts", enable_tls=True)
replay_dir = os.path.join(ts.RunDirectory, "ts", "log")

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'traffic_dump',

    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/signer.pem',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.http.host_sni_policy': 2,
    'proxy.config.ssl.TLSv1_3.enabled': 0,
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLines([
    f'map / http://127.0.0.1:{server.Variables.http_port}'
])

# Configure traffic_dump to dump body bytes (-b).
ts.Disk.plugin_config.AddLine(
    f'traffic_dump.so --logdir {replay_dir} --sample 1 --limit 1000000000 -b'
)

ts_dump_0 = os.path.join(replay_dir, "127", "0000000000000000")
ts.Disk.File(ts_dump_0, exists=True)

ts_dump_1 = os.path.join(replay_dir, "127", "0000000000000001")
ts.Disk.File(ts_dump_1, exists=True)

ts_dump_2 = os.path.join(replay_dir, "127", "0000000000000002")
ts.Disk.File(ts_dump_2, exists=True)

ts_dump_3 = os.path.join(replay_dir, "127", "0000000000000003")
ts.Disk.File(ts_dump_3, exists=True)

ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    "Dumping body bytes: true",
    "Verify that dumping body bytes is enabled.")

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


# Common verification variables.
verify_replay = "verify_replay.py"
schema = os.path.join(Test.Variables.AtsTestToolsDir, 'lib', 'replay_schema.json')
verify_command_prefix = f'{sys.executable} {verify_replay} {schema}'

#
# Verify a response without a body is dumped correctly.
#
tr = Test.AddTestRun("Verify the json content of the transaction with no response body.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = f'{verify_command_prefix} {ts_dump_0}'
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Verify a response with a body is dumped correctly.
#
tr = Test.AddTestRun("Verify the json content of the transaction with a response body.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    f'{verify_command_prefix} {ts_dump_1} --response_body "0000000 0000001 "'
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

#
# Verify a response with a character to be escaped is dumped correctly.
#
tr = Test.AddTestRun("Verify the json content of the response body with a character to be escaped.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    rf'{verify_command_prefix} {ts_dump_2} --response_body 12\"34'
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
#
# Verify an HTTP/2 response with a body is dumped correctly.
#
tr = Test.AddTestRun("Verify the json content of the response body of an HTTP/2 transaction.")
tr.Setup.CopyAs(verify_replay, Test.RunDirectory)
tr.Processes.Default.Command = \
    f'{verify_command_prefix} {ts_dump_3} --response_body "0000000 0000001 0000002 "'
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
