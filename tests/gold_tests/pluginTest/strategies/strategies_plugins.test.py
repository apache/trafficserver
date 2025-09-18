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

# set up dns
dns = Test.MakeDNServer("dns")
dns.ReturnCode = Any(0, -2)

ts_nh = []
num_nh = 3

# Define next hop trafficserver instances.
# strategy, strategy, parent.config
for ind in range(num_nh):
    name = f"nh{ind}"
    ts = Test.MakeATSProcess(name, use_traffic_out=False, command=f"traffic_server 2>nh_trace{ind}.log")
    ts.ReturnCode = Any(0, -2)
    ts.Disk.plugin_config.AddLine("header_rewrite.so hdr_rw.config")
    ts.Disk.MakeConfigFile("hdr_rw.config").AddLines([
        "cond %{READ_REQUEST_HDR_HOOK}",
        "set-status 404",
    ])
    ts.Disk.records_config.update(
        {
            'proxy.config.http.cache.http': 0,
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.http.insert_response_via_str': 1,
            'proxy.config.proxy_name': name,
            'proxy.config.diags.debug.tags': "http|header_rewrite",
            'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
            'proxy.config.dns.resolv_conf': "NULL",
        })
    ts.Disk.remap_config.AddLine("map / http://not_used")
    dns.addRecords(records={name: ["127.0.0.1"]})
    ts_nh.append(ts)

# Define ATS and configure
ts = Test.MakeATSProcess("ts", use_traffic_out=False, command="traffic_server 2>ts_trace.log")
ts.ReturnCode = Any(0, -2)
ts.Disk.records_config.update(
    {
        'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
        'proxy.config.dns.resolv_conf': "NULL",
        'proxy.config.http.cache.http': 0,
        "proxy.config.http.insert_response_via_str": 0,
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
    [f'dest_domain=. parent="nh2:{ts_nh[2].Variables.port}" round_robin=false go_direct=false parent_is_proxy=false'])

# build strategies.yaml file
ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")

s = ts.Disk.strategies
s.AddLine("groups:")
for ind in range(num_nh - 1):
    name = f"nh{ind}"
    s.AddLines(
        [
            f"  - &g{ind}",
            f"    - host: {name}",
            f"      protocol:",
            f"      - scheme: http",
            f"        port: {ts_nh[ind].Variables.port}",
            f"      weight: 1.0",
        ])

s.AddLine("strategies:")

# third ts_nh
for ind in range(num_nh - 1):
    name = f"nh{ind}"
    s.AddLines(
        [
            f"  - strategy: {name}", f"    policy: consistent_hash", f"    hash_key: path", f"    go_direct: false",
            f"    parent_is_proxy: true", f"    ignore_self_detect: true", f"    groups:", f"      - *g{ind}", f"    scheme: http"
        ])

ts.Disk.remap_config.AddLines(
    [
        "map http://nh0/ http://not_used @strategy=nh0",
        "map http://nh1/ http://not_used @strategy=nh1",
        "map http://nh2/ http://not_used",
    ])

# Tests

curl_and_args = '-s -D /dev/stdout -o /dev/stderr -x localhost:{}'.format(ts.Variables.port)

# 0 Test - nh0 default request
tr = Test.AddTestRun("nh0 straight through request")
ps = tr.Processes.Default
tr.Processes.Default.StartBefore(dns)
for ind in range(num_nh):
    tr.Processes.Default.StartBefore(ts_nh[ind])
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(curl_and_args + " http://nh0/req", ts=ts)
ps.ReturnCode = Any(0, -2)
ps.Streams.stdout.Content = Testers.ContainsExpression("nh0", "expected nh0")
tr.StillRunningAfter = ts

# 1 Test - nh1 default request
tr = Test.AddTestRun("nh1 straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh1/req", ts=ts)
ps.ReturnCode = Any(0, -2)
ps.Streams.stdout.Content = Testers.ContainsExpression("nh1", "expected nh1")
tr.StillRunningAfter = ts

# 2 Test - nh2 default request
tr = Test.AddTestRun("nh2 straight through request")
ps = tr.Processes.Default
tr.MakeCurlCommand(curl_and_args + " http://nh2/req", ts=ts)
ps.ReturnCode = Any(0, -2)
ps.Streams.stdout.Content = Testers.ContainsExpression("nh2", "expected nh2")
tr.StillRunningAfter = ts
