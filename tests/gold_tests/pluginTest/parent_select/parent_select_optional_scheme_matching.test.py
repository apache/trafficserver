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
Basic parent_select plugin test
'''

Test.SkipUnless(
    Condition.PluginExists('parent_select.so'),
)
Test.ContinueOnFail = False

# Define and populate MicroServer.
#
server = Test.MakeOriginServer("server")
response_header = {
    "headers":
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Cache-control: max-age=85000\r\n"
        "\r\n",
    "timestamp": "1469733493.993",
    "body": "This is the body.\n"
}
num_objects = 32
for i in range(num_objects):
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

# Define next hop trafficserver instances.
#
num_nh = 8
ts_nh = []
for i in range(num_nh):
    ts = Test.MakeATSProcess(f"ts_nh{i}", use_traffic_out=False, command=f"traffic_server 2>nh_trace{i}.log")
    ts.Disk.records_config.update({
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|dns',
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
    })
    ts.Disk.remap_config.AddLine(
        f"map / http://127.0.0.1:{server.Variables.Port}"
    )
    ts_nh.append(ts)

ts = Test.MakeATSProcess("ts")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|dns|parent|next_hop|host_statuses|hostdb',
    'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",  # Only nameservers if resolv_conf NULL.
    'proxy.config.dns.resolv_conf': "NULL",  # This defaults to /etc/resvolv.conf (OS namesevers) if not NULL.
    'proxy.config.http.cache.http': 0,
    'proxy.config.http.uncacheable_requests_bypass_parent': 0,
    'proxy.config.http.no_dns_just_forward_to_parent': 1,
    'proxy.config.http.parent_proxy.mark_down_hostdb': 0,
    'proxy.config.http.parent_proxy.self_detect': 0,
})

ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")
s = ts.Disk.strategies
s.AddLine("groups:")
s.AddLine("  - &g1")
for i in range(num_nh):
    dns.addRecords(records={f"next_hop{i}": ["127.0.0.1"]})
    s.AddLine(f"    - host: next_hop{i}")
    s.AddLine(f"      protocol:")
    s.AddLine(f"        - port: {ts_nh[i].Variables.port}")
    # The health check URL does not seem to be used currently.
    # s.AddLine(f"          health_check_url: http://next_hop{i}:{ts_nh[i].Variables.port}")
    s.AddLine(f"      weight: 1.0")
s.AddLines([
    "strategies:",
    "  - strategy: the-strategy",
    "    policy: consistent_hash",
    "    hash_key: path",
    "    go_direct: false",
    "    parent_is_proxy: true",
    "    ignore_self_detect: true",
    "    groups:",
    "      - *g1"])

ts.Disk.remap_config.AddLine(
    "map http://dummy.com http://not_used @plugin=parent_select.so @pparam=" +
    ts.Variables.CONFIGDIR +
    "/strategies.yaml @pparam=the-strategy")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
for i in range(num_nh):
    tr.Processes.Default.StartBefore(ts_nh[i])
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'echo start TS, HTTP server, DNS server and next hop TSes'
tr.Processes.Default.ReturnCode = 0

for i in range(num_objects):
    tr = Test.AddTestRun()
    tr.Processes.Default.Command = (
        f'curl --verbose --proxy 127.0.0.1:{ts.Variables.port} http://dummy.com/obj{i}'
    )
    tr.Processes.Default.Streams.stdout = "body.gold"
    tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
# For some reason, the * won't be expanded when the command is executed, if stdout is not piped through "cat".
tr.Processes.Default.Command = "grep -F '200 OK' nh_trace*.log | cat"
tr.Processes.Default.Streams.stdout = "trace.gold"
tr.Processes.Default.ReturnCode = 0
