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

import sys

Test.Summary = '''
Verify that when an origin responds before consuming the request body on a
connection with Expect: 100-continue, ATS does not return the origin connection
to the pool with unconsumed data.
'''

tr = Test.AddTestRun('Verify 100-continue with early origin response does not corrupt pooled connections.')

# DNS.
dns = tr.MakeDNServer('dns', default='127.0.0.1')

# Origin.
Test.GetTcpPort('origin_port')
tr.Setup.CopyAs('corruption_origin.py')
origin = tr.Processes.Process(
    'origin', f'{sys.executable} corruption_origin.py '
    f'{Test.Variables.origin_port} --delay 1.0 --timeout 5.0')
origin.Ready = When.PortOpen(Test.Variables.origin_port)

# ATS.
ts = tr.MakeATSProcess('ts', enable_cache=False)
ts.Disk.remap_config.AddLine(f'map / http://backend.example.com:{Test.Variables.origin_port}')
ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http',
        'proxy.config.dns.nameservers': f'127.0.0.1:{dns.Variables.Port}',
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.http.send_100_continue_response': 1,
    })

# Client.
tr.Setup.CopyAs('corruption_client.py')
tr.Setup.CopyAs('http_utils.py')
tr.Processes.Default.Command = (
    f'{sys.executable} corruption_client.py '
    f'127.0.0.1 {ts.Variables.port} '
    f'-s backend.example.com')
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(origin)
tr.Processes.Default.StartBefore(ts)

# With the fix, ATS should not pool the origin connection when the
# request body was not fully consumed, preventing corruption.
tr.Processes.Default.Streams.stdout += Testers.ContainsExpression(
    'No corruption', 'The second request should complete normally because ATS '
    'does not pool origin connections with unconsumed body data.')
tr.Processes.Default.Streams.stdout += Testers.ExcludesExpression('Corruption detected', 'No corruption should be detected.')
