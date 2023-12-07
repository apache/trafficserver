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
Test next hop using strategies.yaml with consistent hashing, with peering, and no upstream group"
'''

# The tls_conn_timeout test will fail if it runs before this test in CI.  Therefore, this test has a zzz
# prefix so it will run last in CI.

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
num_object = 16
for i in range(num_object):
    request_header = {
        "headers":
            f"GET /obj{i} HTTP/1.1\r\n"
            "Host: does.not.matter\r\n"  # But cannot be omitted.
            "\r\n",
        "timestamp": "1469733493.993",
        "body": ""
    }
    server.addResponse("sessionlog.json", request_header, response_header)

dns = Test.MakeDNServer("dns")

# Define upstream trafficserver instances.
#
num_upstream = 6
ts_upstream = []
for i in range(num_upstream):
    ts = Test.MakeATSProcess(f"ts_upstream{i}")
    dns.addRecords(records={f"ts_upstream{i}": ["127.0.0.1"]})
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|dns',
            'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
            'proxy.config.dns.resolv_conf': "NULL",
        })
    ts.Disk.remap_config.AddLine(f"map / http://127.0.0.1:{server.Variables.Port}")
    ts_upstream.append(ts)

# Define peer trafficserver instances.
#
num_peer = 8
ts_peer = []
for i in range(num_peer):
    ts = Test.MakeATSProcess(f"ts_peer{i}", use_traffic_out=False, command=f"traffic_server 2> trace_peer{i}.log")
    ts_peer.append(ts)
for i in range(num_peer):
    ts = ts_peer[i]
    dns.addRecords(records={f"ts_peer{i}": ["127.0.0.1"]})

    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'http|dns|parent|next_hop|host_statuses|hostdb',
            'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",  # Only nameservers if resolv_conf NULL.
            'proxy.config.dns.resolv_conf': "NULL",  # This defaults to /etc/resvolv.conf (OS namesevers) if not NULL.
            'proxy.config.http.cache.http': 1,
            'proxy.config.http.cache.required_headers': 0,
            'proxy.config.http.uncacheable_requests_bypass_parent': 0,
            'proxy.config.http.no_dns_just_forward_to_parent': 0,
            'proxy.config.http.parent_proxy.mark_down_hostdb': 0,
            'proxy.config.http.parent_proxy.self_detect': 1,
        })

    ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")
    s = ts.Disk.strategies
    s.AddLine("groups:")
    s.AddLine("  - &peer_group")
    for j in range(num_peer):
        s.AddLine(f"    - host: ts_peer{j}")
        s.AddLine(f"      protocol:")
        s.AddLine(f"        - scheme: http")
        s.AddLine(f"          port: {ts_peer[j].Variables.port}")
        # The health check URL does not seem to be used currently.
        # s.AddLine(f"          health_check_url: http://ts_peer{j}:{ts_peer[j].Variables.port}")
        s.AddLine(f"      weight: 1.0")
    s.AddLines(
        [
            "strategies:",
            "  - strategy: the-strategy",
            "    policy: consistent_hash",
            "    hash_key: path",
            "    go_direct: true",
            "    parent_is_proxy: true",
            "    cache_peer_result: false",
            "    ignore_self_detect: false",
            "    groups:",
            "      - *peer_group",
            "    scheme: http",
            "    failover:",
            "      ring_mode: peering_ring",
            f"      self: ts_peer{i}",
            #"      max_simple_retries: 2",
            #"      response_codes:",
            #"        - 404",
            #"      health_check:",
            #"        - passive",
        ])

    for i in range(num_upstream):
        prefix = f"http://ts_upstream{i}:{ts_upstream[i].Variables.port}/"
        ts.Disk.remap_config.AddLine(f"map {prefix} {prefix} @strategy=the-strategy")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
for i in range(num_peer):
    tr.Processes.Default.StartBefore(ts_peer[i])
for i in range(num_upstream):
    tr.Processes.Default.StartBefore(ts_upstream[i])
tr.Processes.Default.Command = 'echo start peer and upstream TSes, HTTP server and DNS server'
tr.Processes.Default.ReturnCode = 0

for i in range(num_object):
    tr = Test.AddTestRun()
    tr.Processes.Default.Command = (
        f'curl --verbose --proxy 127.0.0.1:{ts_peer[i % num_peer].Variables.port} http://ts_upstream0:{ts_upstream[0].Variables.port}/obj{i}'
    )
    tr.Processes.Default.Streams.stdout = "body.gold"
    tr.Processes.Default.ReturnCode = 0

for i in range(num_object):
    tr = Test.AddTestRun()
    # num_peer must not be a multiple of 3
    tr.Processes.Default.Command = (
        f'curl --verbose --proxy 127.0.0.1:{ts_peer[(i * 3) % num_peer].Variables.port} http://ts_upstream0:{ts_upstream[0].Variables.port}/obj{i}'
    )
    tr.Processes.Default.Streams.stdout = "body.gold"
    tr.Processes.Default.ReturnCode = 0

normalize_ports = ""
for i in range(num_upstream):
    normalize_ports += f" | sed 's/:{ts_upstream[i].Variables.port}/:UP_PORT{i}/'"

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    "grep -e '^+++' -e '^[A-Z].*TTP/' -e '^.alts. --' -e 'PARENT_SPECIFIED' trace_peer*.log"
    " | sed 's/^.*(next_hop) [^ ]* //' | sed 's/[.][0-9]*$$//' " + normalize_ports)
tr.Processes.Default.Streams.stdout = "trace.gold"
tr.Processes.Default.ReturnCode = 0
