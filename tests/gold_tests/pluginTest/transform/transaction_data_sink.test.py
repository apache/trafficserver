'''
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

Test.Summary = '''
Verify transaction data sink.
'''

Test.SkipUnless(Condition.PluginExists('txn_data_sink.so'),)

replay_file = "transaction-with-body.replays.yaml"
server = Test.MakeVerifierServerProcess("server", replay_file)
nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'txn_data_sink',
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
    })
ts.Disk.remap_config.AddLine(f'map / http://localhost:{server.Variables.http_port}/')
ts.Disk.plugin_config.AddLine('txn_data_sink.so')

# Verify that the various aspects of the expected debug output for the
# transaction are logged.
ts.Disk.traffic_out.Content = Testers.ContainsExpression(
    '"http1.1_response_body"', "The response body should be printed by the plugin.")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(nameserver)
tr.AddVerifierClientProcess("client-1", replay_file, http_ports=[ts.Variables.port])
