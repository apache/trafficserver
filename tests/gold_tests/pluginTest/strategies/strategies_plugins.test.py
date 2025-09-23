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
Combined header_rewrite/regex_remap/tslua strategies tests
'''

# Test description:
# Preload the cache with the entire asset to be range requested.
# Reload remap rule with slice plugin
# Request content through the slice plugin

Test.SkipUnless(
    Condition.PluginExists('header_rewrite.so'),
    Condition.PluginExists('regex_remap.so'),
    #Condition.PluginExists('tslua.so'),
)
Test.ContinueOnFail = False

dns = Test.MakeDNServer("dns")

origins = []
num_origins = 3
for ind in range(num_origins):
    name = f"nh{ind}"
    origin = Test.MakeOriginServer(name, options={"--verbose": ""})
    request_header = {
        "headers": f"GET / HTTP/1.1\r\nHost: origin\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    response_header = {
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    request_header = {
        "headers": "GET /path HTTP/1.1\r\nHost: origin\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    response_header = {
        "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nOrigin: {name}\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": name,
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    request_header = {
        "headers": f"GET /path/{name} HTTP/1.1\r\nHost: origin\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": "",
    }
    response_header = {
        "headers": f"HTTP/1.1 200 OK\r\nConnection: close\r\nOrigin: {name}\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": name,
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    origin.ReturnCode = 0
    origins.append(origin)
    dns.addRecords(records={name: ["127.0.0.1"]})

# Define ATS and configure
ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.ReturnCode = 0
ts.Disk.records_config.update(
    {
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
        'proxy.config.http.cache.http': 0,
        "proxy.config.http.insert_response_via_str": 1,
        'proxy.config.http.uncacheable_requests_bypass_parent': 0,
        'proxy.config.http.no_dns_just_forward_to_parent': 1,
        'proxy.config.http.parent_proxy.mark_down_hostdb': 0,
        'proxy.config.http.parent_proxy.self_detect': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': "url_rewrite|next_hop|dns|parent|regex_remap|header_rewrite|tslua|http|hostdb",
    })

ts.Disk.MakeConfigFile("hdr_rw.config").AddLines(
    [
        "cond %{REMAP_PSEUDO_HOOK}",
        'cond %{CLIENT-HEADER:Strategy} ="" [NOT]',
        "set-next-hop-strategy %{CLIENT-HEADER:Strategy}",
    ])
ts.Disk.MakeConfigFile("regex_remap.config").AddLines(
    [
        "/nh0 http://origin/path @strategy=nh1",
        '/nh1 http://origin/path @strategy=',
        "/nh2 http://origin/path @strategy=nh0",
    ])
ts.Disk.MakeConfigFile("strategies.lua").AddLines(
    [
        'function do_remap()',
        ' local uri = ts.client_request.get_uri()',
        ' if uri:find("nh0") then',
        '  ts.http.set_next_hop_strategy("nh1")',
        ' elseif uri:find("nh1") then',
        '  ts.http.set_next_hop_strategy("")',
        ' elseif uri:find("nh2") then',
        '  ts.http.set_next_hop_strategy("nh0")',
        ' end',
        ' ts.client_request.set_uri("path")',
        ' return 0',
        'end',
    ])

# parent.config
ts.Disk.parent_config.AddLines(
    [f'dest_domain=. parent="nh2:{origins[2].Variables.Port}" round_robin=false go_direct=false parent_is_proxy=false'])

# build strategies.yaml file
ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")

s = ts.Disk.strategies
s.AddLine("groups:")
for ind in range(num_origins - 1):
    name = f"nh{ind}"
    s.AddLines(
        [
            f"  - &g{ind}",
            f"    - host: {name}",
            f"      protocol:",
            f"      - scheme: http",
            f"        port: {origins[ind].Variables.Port}",
            f"      weight: 1.0",
        ])

s.AddLine("strategies:")

# third ts_nh
for ind in range(num_origins - 1):
    s.AddLines(
        [
            f"  - strategy: nh{ind}",
            f"    policy: consistent_hash",
            f"    hash_key: path",
            f"    go_direct: false",
            f"    parent_is_proxy: false",
            f"    ignore_self_detect: true",
            f"    groups:",
            f"      - *g{ind}",
            f"    scheme: http",
        ])

ts.Disk.remap_config.AddLines(
    [
        "map http://nh0_hr http://origin @strategy=nh0 @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "map http://nh1_hr http://origin @strategy=nh1 @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "map http://nh2_hr http://origin @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "map http://nh0_rr http://origin @strategy=nh0 @plugin=regex_remap.so @pparam=regex_remap.config",
        "map http://nh1_rr http://origin @strategy=nh1 @plugin=regex_remap.so @pparam=regex_remap.config",
        "map http://nh2_rr http://origin @plugin=regex_remap.so @pparam=regex_remap.config",
        "map http://nh0_lua http://origin @strategy=nh0 @plugin=tslua.so @pparam=strategies.lua",
        "map http://nh1_lua http://origin @strategy=nh1 @plugin=tslua.so @pparam=strategies.lua",
        "map http://nh2_lua http://origin @plugin=tslua.so @pparam=strategies.lua",
    ])

# Tests

# stdout for body, stderr for headers
curl_and_args = '-s -o /dev/stdout -D /dev/stderr -x localhost:{}'.format(ts.Variables.port)

# header rewrite

# 0 - nh0 default request
tr = Test.AddTestRun("nh0_hr straight through request")
ps = tr.Processes.Default
for ind in range(num_origins):
    origin = origins[ind]
    ps.StartBefore(origin, ready=When.PortOpen(origin.Variables.Port))
    tr.StillRunningAfter = origin
ps.StartBefore(dns)
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + " http://nh0_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 1 - nh1_hr default request
tr = Test.AddTestRun("nh1_hr straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh1_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts

# 2 - nh2_hr default request
tr = Test.AddTestRun("nh2_hr straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh2_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts

# 3 switch strategies
tr = Test.AddTestRun("nh0_hr switch to nh1")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_hr/path -H "Strategy: nh1"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 4 strategy to parent.config
tr = Test.AddTestRun("nh1_hr switch to parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh1_hr/path -H "Strategy: null"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 5 parent.config strategy to strategy
tr = Test.AddTestRun("nh2_hr switch to nh0")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh2_hr/path -H "Strategy: nh0"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# regex_remap

# 6 switch strategies
tr = Test.AddTestRun("nh0_rr switch to nh1")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_rr/nh0', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# ' strategy to parent.config
tr = Test.AddTestRun("nh1_rr switch to parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh1_rr/nh1', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 8 parent.config strategy to strategy
tr = Test.AddTestRun("nh2_rr switch to nh0")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh2_rr/nh2', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# tslua

# 9 switch strategies
tr = Test.AddTestRun("nh0_lua switch to nh1")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_lua/nh0', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 10 strategy to parent.config
tr = Test.AddTestRun("nh1_lua switch to parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh1_lua/nh1', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 11 parent.config strategy to strategy
tr = Test.AddTestRun("nh2_lua switch to nh0")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh2_lua/nh2', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns
