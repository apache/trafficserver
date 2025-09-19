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
        "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
        "timestamp": "1469733493.993",
        "body": name,
    }
    origin.addResponse("sessionfile.log", request_header, response_header)
    origin.ReturnCode = Any(0, -2)
    origins.append(origin)
    dns.addRecords(records={name: ["127.0.0.1"]})

# Define ATS and configure
ts = Test.MakeATSProcess("ts", enable_cache=False)
ts.ReturnCode = Any(0, -2)
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

ts.Disk.plugin_config.AddLine("header_rewrite.so hdr_rw.config")
ts.Disk.MakeConfigFile("hdr_rw.config").AddLines(
    [
        "cond %{READ_REQUEST_HDR_HOOK}",
        'cond %{CLIENT-HEADER:Strategy} ="" [NOT]',
        "set-next-hop-strategy %{CLIENT-HEADER:Strategy}",
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
            f"  - strategy: strategy-{ind}",
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
        "map http://nh0 http://origin @strategy=strategy-0",
        "map http://nh1 http://origin @strategy=strategy-1",
        "map http://nh2 http://nh2",
    ])

# Tests

# stdout for body, stderr for headers
curl_and_args = '-s -o /dev/stdout -D /dev/stderr -x localhost:{}'.format(ts.Variables.port)

# 0 Test - nh0 default request
tr = Test.AddTestRun("nh0 straight through request")
ps = tr.Processes.Default
for ind in range(num_origins):
    origin = origins[ind]
    ps.StartBefore(origin, ready=When.PortOpen(origin.Variables.Port))
    tr.StillRunningAfter = origin
ps.StartBefore(dns)
ps.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + " http://nh0/path", ts=ts)
ps.ReturnCode = Any(0, -2)
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts
tr.StillRunnerAfter = dns

# 1 Test - nh1 default request
tr = Test.AddTestRun("nh1 straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh1/path", ts=ts)
ps.ReturnCode = Any(0, -2)
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts

# 2 Test - nh2 default request
tr = Test.AddTestRun("nh2 straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh2/path", ts=ts)
ps.ReturnCode = Any(0, -2)
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
