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

Test.Summary = '''
Test next hop consistent hashing with 5 rings (MAX_GROUP_RINGS).
Validates mapWrapped and chashIter work at full ring capacity.
'''

# Define and populate MicroServer.
#
server = Test.MakeOriginServer("server")
response_header = {
    "headers": "HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Cache-control: max-age=85000\r\n"
               "\r\n",
    "timestamp": "1469733493.993",
    "body": "This is the body.\n"
}
num_objects = 32
for i in range(num_objects):
    request_header = {
        "headers": f"GET /obj{i} HTTP/1.1\r\n"
                   "Host: does.not.matter\r\n"
                   "\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    server.addResponse("sessionlog.json", request_header, response_header)

dns = Test.MakeDNServer("dns")

# 5 rings, 2 hosts each = 10 next hops.
# only the last ring will be started
#
num_rings = 5
hosts_per_ring = 2
num_hosts = num_rings * hosts_per_ring
ts_nh = []
for ii in range(num_hosts):
    ts = Test.MakeATSProcess(f"ts_nh{ii}")
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|dns',
            'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
            'proxy.config.dns.resolv_conf': "NULL",
        })
    line = f"map / http://127.0.0.1:{server.Variables.Port}"
    if ii < (num_hosts - hosts_per_ring):
        line += " @plugin=header_rewrite.so @pparam=hdr_rw.conf"
    ts.Disk.remap_config.AddLine(line)
    ts.Disk.MakeConfigFile("hdr_rw.conf").AddLine("set-status 502")
    ts_nh.append(ts)

ts = Test.MakeATSProcess("ts", use_traffic_out=False, command="traffic_server 2> trace.log")
ts.ReturnCode = Any(0, -2)

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns|parent|next_hop|host_statuses|hostdb',
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
        'proxy.config.http.cache.http': 0,
        'proxy.config.http.parent_proxy.per_parent_connect_attempts': 1,
        'proxy.config.http.uncacheable_requests_bypass_parent': 0,
        'proxy.config.http.no_dns_just_forward_to_parent': 1,
        'proxy.config.http.parent_proxy.mark_down_hostdb': 0,
        'proxy.config.http.down_server.cache_time': 1,
        'proxy.config.http.parent_proxy.self_detect': 0,
    })

ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")
s = ts.Disk.strategies
s.AddLine("groups:")

# Build 5 groups.
idx = 0
for ring in range(num_rings):
    s.AddLine(f"  - &g{ring}")
    for h in range(hosts_per_ring):
        dns.addRecords(records={f"next_hop_{idx}": ["127.0.0.1"]})
        s.AddLine(f"    - host: next_hop_{idx}")
        s.AddLine(f"      protocol:")
        s.AddLine(f"        - scheme: http")
        s.AddLine(f"          port: {ts_nh[idx].Variables.port}")
        s.AddLine(f"      weight: 1.0")
        idx = idx + 1

strategy_lines = [
    "strategies:",
    "  - strategy: the-strategy",
    "    policy: consistent_hash",
    "    hash_key: path",
    "    go_direct: false",
    "    parent_is_proxy: true",
    "    ignore_self_detect: true",
    "    scheme: http",
    "    failover:",
    "      ring_mode: alternate_ring",
    "      max_simple_retries: 5",
    "      response_codes: [404]",
    "      max_unavailable_retries: 5",
    "      markdown_codes: [502]",
    "    groups:",
]
for ring in range(num_rings):
    strategy_lines.append(f"      - *g{ring}")
s.AddLines(strategy_lines)

ts.Disk.remap_config.AddLine("map http://dummy.com http://not_used @strategy=the-strategy")

# Only start ring 5 (last ring, indices 8-9). Forces fallover through all 5 rings.
tr = Test.AddTestRun()
ps = tr.Processes.Default
ps.StartBefore(server)
ps.StartBefore(dns)
#for idx in range((num_rings - 1) * hosts_per_ring, len(ts_nh)):
for idx in range(len(ts_nh)):
    ps.StartBefore(ts_nh[idx])
ps.StartBefore(Test.Processes.ts)
ps.Command = 'echo start TS, origin, DNS, and only last-ring next hops'
ps.ReturnCode = 0

# Send requests - must fall through rings 1-4 (down) to ring 5.
for i in range(num_objects):
    tr = Test.AddTestRun()
    tr.MakeCurlCommand(f'--verbose --proxy 127.0.0.1:{ts.Variables.port} http://dummy.com/obj{i}', ts=ts)
    ps = tr.Processes.Default
    ps.Streams.stdout.Content = Testers.ContainsExpression("This is the body.", "expected body")
    ps.ReturnCode = 0

tr = Test.AddTestRun()
ps = tr.Processes.Default
ps.Command = ("grep -F ParentResultType::SPECIFIED trace.log | sed 's/^.*(next_hop) [^ ]* //' | sed 's/[.][0-9]*$$//'")
ps.Streams.stdout = "trace.gold"
ps.ReturnCode = 0
