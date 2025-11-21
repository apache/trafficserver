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
    Condition.PluginExists('tslua.so'),
)
Test.ContinueOnFail = False

dns = Test.MakeDNServer("dns")

origins = []

chars = ['0', '1', '2', 'p', 's']
for char in chars:
    name = f"nh{char}"
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
        'proxy.config.diags.debug.tags': "next_hop|dns|http|parent|regex_remap|header_rewrite|tslua",
    })

ts.Disk.MakeConfigFile("hdr_rw.config").AddLines(
    [
        'cond %{CLIENT-HEADER:Strategy} ="nemo"',
        "set-next-hop-strategy nemo",
        'cond %{CLIENT-HEADER:Strategy} ="nh0"',
        "set-next-hop-strategy nh0",
        'cond %{CLIENT-HEADER:Strategy} ="nh1"',
        "set-next-hop-strategy nh1",
        'cond %{CLIENT-HEADER:Strategy} ="null"',
        "set-next-hop-strategy null",
        'cond %{CLIENT-HEADER:Strategy} ="clear"',
        'set-next-hop-strategy ""',
    ])
ts.Disk.MakeConfigFile("regex_remap.config").AddLines(
    [
        "/nh0 http://origin/path @strategy=nh1",
        '/nh1 http://origin/path @strategy=',
        "/nh2 http://origin/path @strategy=nh0",
        '/null http://origin/path @strategy=null',
        "/nemo http://origin/path @strategy=nemo",
        "# fallthrough",
        "/ http://origin/path",
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
        ' elseif uri:find("null") then',
        '  ts.http.set_next_hop_strategy("null")',
        ' elseif uri:find("nemo") then',
        '  ts.http.set_next_hop_strategy("nemo")',
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
for ind in range(len(origins)):
    char = chars[ind]
    org = origins[ind]
    name = f"nh{chars[ind]}"
    s.AddLines(
        [
            f"  - &g{char}",
            f"    - host: {name}",
            f"      protocol:",
            f"      - scheme: http",
            f"        port: {org.Variables.Port}",
            f"      weight: 1.0",
        ])

s.AddLine("strategies:")

# third ts_nh
for char in chars:
    s.AddLines(
        [
            f"  - strategy: nh{char}",
            f"    policy: consistent_hash",
            f"    hash_key: path",
            f"    go_direct: false",
            f"    parent_is_proxy: false",
            f"    ignore_self_detect: true",
            f"    groups:",
            f"      - *g{char}",
            f"    scheme: http",
        ])

ts.Disk.remap_config.AddLines(
    [
        "# header rewrite",
        "map http://nhp_hr http://origin @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "map http://nhs_hr http://origin @strategy=nh0 @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "# modify strategy/parent",
        "map http://nh0_hr http://origin @strategy=nh0 @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "map http://nh1_hr http://origin @strategy=nh1 @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "map http://nh2_hr http://origin @plugin=header_rewrite.so @pparam=hdr_rw.config",
        "",
        "# regex_remap",
        "map http://nhp_rr http://origin @plugin=regex_remap.so @pparam=regex_remap.config",
        "map http://nhs_rr http://origin @strategy=nh0 @plugin=regex_remap.so @pparam=regex_remap.config",
        "# modify strategy/parent",
        "map http://nh0_rr http://origin @strategy=nh0 @plugin=regex_remap.so @pparam=regex_remap.config",
        "map http://nh1_rr http://origin @strategy=nh1 @plugin=regex_remap.so @pparam=regex_remap.config",
        "map http://nh2_rr http://origin @plugin=regex_remap.so @pparam=regex_remap.config",
        "",
        "# tslua",
        "map http://nhp_lua http://origin @plugin=tslua.so @pparam=strategies.lua",
        "map http://nhs_lua http://origin @strategy=nh0 @plugin=tslua.so @pparam=strategies.lua",
        "# modify strategy/parent",
        "map http://nh0_lua http://origin @strategy=nh0 @plugin=tslua.so @pparam=strategies.lua",
        "map http://nh1_lua http://origin @strategy=nh1 @plugin=tslua.so @pparam=strategies.lua",
        "map http://nh2_lua http://origin @plugin=tslua.so @pparam=strategies.lua",
    ])

# Tests

# stdout for body, stderr for headers
curl_and_args = '-s -o /dev/stdout -D /dev/stderr -x localhost:{}'.format(ts.Variables.port)

# header rewrite

# 0 - nhp request to parent.config
tr = Test.AddTestRun("nhp_hr parent.config through request")
ps = tr.Processes.Default
for ind in range(len(origins)):
    origin = origins[ind]
    ps.StartBefore(origin, ready=When.PortOpen(origin.Variables.Port))
    tr.StillRunningAfter = origin
ps.StartBefore(dns)
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + " http://nhp_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 1 - nhs_hr default request
tr = Test.AddTestRun("nhs_hr straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nhs_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts

# 2 - nh0_hr default request
tr = Test.AddTestRun("nh0_hr straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh0_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts

# 3 - nh1_hr default request
tr = Test.AddTestRun("nh1_hr straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh1_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts

# 4 - nh2_hr default request
tr = Test.AddTestRun("nh2_hr straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh2_hr/path", ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts

# 5 switch strategies
tr = Test.AddTestRun("nh0_hr switch to nh1")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_hr/path -H "Strategy: nh1"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 6 strategy to parent.config
tr = Test.AddTestRun("nh1_hr switch to parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh1_hr/path -H "Strategy: null"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 7 parent.config strategy to strategy
tr = Test.AddTestRun("nh2_hr switch to nh0")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh2_hr/path -H "Strategy: nh0"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 8 try to switch to non existent strategy
tr = Test.AddTestRun("nh0_hr switch to nemo (fail)")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_hr/path -H "Strategy: nemo"', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# regex_remap

# 9 use parent.config
tr = Test.AddTestRun("nhp_rr parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nhp_rr/nhp', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 10 use strategies
tr = Test.AddTestRun("nhs_rr strategies.yaml")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nhs_rr/nh', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 11 switch strategies
tr = Test.AddTestRun("nh0_rr switch to nh1")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_rr/nh0', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 12 strategy to parent.config
tr = Test.AddTestRun("nh1_rr switch to parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh1_rr/nh1', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 13 parent.config strategy to strategy
tr = Test.AddTestRun("nh2_rr switch to nh0")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh2_rr/nh2', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 14 switch strategies (fail)
tr = Test.AddTestRun("nh0_rr switch to nemo")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_rr/nemo', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# tslua

# 15 parent.config
tr = Test.AddTestRun("nhp_lua parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nhp_lua/nh', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 16 strategies.yaml
tr = Test.AddTestRun("nhs_lua strategies.yaml")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nhs_lua/nh', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 17 switch strategies
tr = Test.AddTestRun("nh0_lua switch to nh1")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_lua/nh0', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 18 strategy to parent.config
tr = Test.AddTestRun("nh1_lua switch to parent.config")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh1_lua/nh1', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 19 parent.config strategy to strategy
tr = Test.AddTestRun("nh2_lua switch to nh0")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh2_lua/nh2', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# 20 switch strategies, fail
tr = Test.AddTestRun("nh0_lua switch to nemo")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + ' http://nh0_lua/nemo', ts=ts)
ps.ReturnCode = 0
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunningAfter = dns

# Overriding the built in ERROR check since we expect some ERROR messages
ts.Disk.diags_log.Content = Testers.ContainsExpression("ERROR", "Some tests are failure tests")
