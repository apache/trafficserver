'''
Verify Split DNS fails over from a dead nameserver to a live one and recovers.
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

Test.Summary = 'Verify Split DNS fails over from a dead nameserver to a live one.'

HOSTS = ['a', 'b', 'c', 'd']
records = {f'{host}.ts.a.o.': ['127.0.0.1'] for host in HOSTS}

dead_dns = Test.MakeDNServer('dead_dns')
dead_dns.addRecords(records=records)
live_dns = Test.MakeDNServer('live_dns')
live_dns.addRecords(records=records)

origin = Test.MakeOriginServer('origin')
origin.addResponse(
    'sessionlog.json', {'headers': 'GET / HTTP/1.1\r\nHost: x\r\n\r\n'},
    {'headers': 'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n'})

ts = Test.MakeATSProcess('ts', enable_cache=False)
ts.Disk.records_config.update(
    {
        'proxy.config.dns.splitDNS.enabled': 1,
        'proxy.config.dns.round_robin_nameservers': 1,
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'dns|splitdns',
    })
ts.Disk.splitdns_config.AddLine(
    f'dest_domain=ts.a.o named="127.0.0.1:{dead_dns.Variables.Port} '
    f'127.0.0.1:{live_dns.Variables.Port}"')
for host in HOSTS:
    ts.Disk.remap_config.AddLine(f'map /{host}/ http://{host}.ts.a.o:{origin.Variables.Port}/')
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    'connection to DNS server .* lost, marking as down', 'the dead nameserver was queried and marked down')


def request(host, title):
    tr = Test.AddTestRun(title)
    tr.MakeCurlCommand(f'-sS -i -m 60 http://localhost:{ts.Variables.port}/{host}/', ts=ts)
    tr.Processes.Default.ReturnCode = 0
    tr.Processes.Default.Streams.stdout = Testers.ContainsExpression('HTTP/1.1 200 OK', f'{host}.ts.a.o resolved')
    tr.StillRunningAfter = ts
    return tr


# Round robin advances before sending, so three lookups ensure one uses the dead server.
tr = request('a', 'first lookup')
tr.Processes.Default.StartBefore(live_dns)
tr.Processes.Default.StartBefore(origin)
tr.Processes.Default.StartBefore(ts)
request('b', 'second lookup')
request('c', 'third lookup')

# Bring the dead nameserver up and wait past DNS_PRIMARY_RETRY_PERIOD.
tr = Test.AddTestRun('start the dead nameserver')
tr.Processes.Default.Command = 'sleep 12'
tr.Processes.Default.StartBefore(dead_dns)
tr.StillRunningAfter = ts
request('d', 'lookup after recovery')
