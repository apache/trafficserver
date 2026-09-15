'''
Verify HostDB can be cleared without restarting Traffic Server.
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

import re

from ports import get_port

Test.Summary = 'Verify HostDB can be cleared without restarting Traffic Server.'

replay_file = 'replay/single_transaction.replay.yaml'
hostname = 'resolve.this.com'
hostdb_items_metric = 'proxy.process.hostdb.cache.current_items'

server = Test.MakeVerifierServerProcess('server', replay_file)
ts = Test.MakeATSProcess('ts', enable_cache=False)
dns_port = get_port(ts, 'dns_port')
dns = Test.MakeDNServer('dns', port=dns_port)
dns.addRecords(records={hostname: ['127.0.0.1']})

ts.Disk.records_config.update({
    'proxy.config.dns.nameservers': f'127.0.0.1:{dns_port}',
    'proxy.config.dns.resolv_conf': 'NULL',
})
ts.Disk.remap_config.AddLine(f'map / http://{hostname}:{server.Variables.http_port}/')

tr = Test.AddTestRun('Populate HostDB')
tr.AddVerifierClientProcess('client-before-clear', replay_file, http_ports=[ts.Variables.port])
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Verify HostDB is populated')
tr.Processes.Default.Command = f'traffic_ctl hostdb status {hostname}'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(hostname, 'HostDB should contain the resolved host')
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Clear HostDB')
tr.Processes.Default.Command = 'traffic_ctl hostdb clear'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Verify HostDB is empty')
tr.Processes.Default.Command = f'traffic_ctl metric get {hostdb_items_metric}'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    rf'{hostdb_items_metric} 0$', 'The HostDB current-items metric should be zero after clearing', reflags=re.MULTILINE)
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Clear HostDB while a transaction is in flight')
tr.Processes.Default.Command = (
    f'curl --fail --silent --show-error --output /dev/null '
    f'--header "Host: {hostname}" --header "X-Request: request" --header "uuid: 1" '
    f'http://127.0.0.1:{ts.Variables.port}/some/path & '
    'client_pid=$$!; '
    f'for attempt in $$(seq 1 100); do traffic_ctl hostdb status {hostname} | grep -q {hostname} && break; sleep 0.05; done; '
    f'traffic_ctl hostdb status {hostname} | grep -q {hostname}; status_result=$$?; '
    'traffic_ctl hostdb clear; clear_result=$$?; '
    'wait "$$client_pid"; client_result=$$?; '
    'test "$$status_result" -eq 0 -a "$$clear_result" -eq 0 -a "$$client_result" -eq 0')
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Reject arguments to HostDB clear')
tr.Processes.Default.Command = 'traffic_ctl hostdb clear extra_arg'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 64
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Resolve the host again without restarting Traffic Server')
tr.AddVerifierClientProcess('client-after-clear', replay_file, http_ports=[ts.Variables.port])
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

tr = Test.AddTestRun('Verify HostDB is repopulated')
tr.Processes.Default.Command = f'traffic_ctl hostdb status {hostname}'
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression(hostname, 'HostDB should contain the resolved host again')
tr.StillRunningAfter = dns
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

ts.Disk.diags_log.Content += Testers.ContainsExpression('HostDB cleared via JSONRPC', 'HostDB clears should be logged')
