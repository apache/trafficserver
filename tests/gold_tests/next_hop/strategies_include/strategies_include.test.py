'''
Verify '#include' handling in strategies.yaml.
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
Verify that strategies.yaml '#include' paths resolve relative to the configuration
directory.
'''

server = Test.MakeOriginServer("server")
request_header = {
    "headers": "GET /path HTTP/1.1\r\n"
               "Host: strategy.test\r\n"
               "\r\n",
    "timestamp": "1469733493.993",
    "body": ""
}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Cache-control: max-age=85000\r\n"
               "\r\n",
    "timestamp": "1469733493.993",
    "body": "This is the body.\n"
}
server.addResponse("sessionlog.json", request_header, response_header)

dns = Test.MakeDNServer("dns")
dns.addRecords(records={"nexthop.test": ["127.0.0.1"]})


def make_ts(name: str) -> 'Process':
    ts = Test.MakeATSProcess(name)
    ts.Disk.records_config.update(
        {
            'proxy.config.diags.debug.enabled': 1,
            'proxy.config.diags.debug.tags': 'next_hop|url_rewrite',
            'proxy.config.dns.nameservers': f"127.0.0.1:{dns.Variables.Port}",
            'proxy.config.dns.resolv_conf': "NULL",
            'proxy.config.http.cache.http': 0,
            'proxy.config.http.uncacheable_requests_bypass_parent': 0,
            'proxy.config.http.no_dns_just_forward_to_parent': 1,
            'proxy.config.http.parent_proxy.self_detect': 0,
        })
    ts.Disk.remap_config.AddLine("map http://strategy.test http://not_used @strategy=the-strategy")
    return ts


strategy_lines = [
    "strategies:",
    "  - strategy: the-strategy",
    "    policy: first_live",
    "    go_direct: false",
    "    parent_is_proxy: false",
    "    ignore_self_detect: true",
    "    groups:",
    "      - *g1",
    "    scheme: http",
]

# A strategies.yaml that includes hosts.yaml from the configuration directory.
# The traffic_server working directory is not the configuration directory, so
# this only works when includes resolve relative to the configuration directory.
ts_include = make_ts("ts_include")
ts_include.Disk.File(ts_include.Variables.CONFIGDIR + "/hosts.yaml", id="hosts", typename="ats:config")
ts_include.Disk.hosts.AddLines(
    [
        "groups:",
        "  - &g1",
        "    - host: nexthop.test",
        "      protocol:",
        "        - scheme: http",
        f"          port: {server.Variables.Port}",
        "      weight: 1.0",
    ])
ts_include.Disk.File(ts_include.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")
ts_include.Disk.strategies.AddLine("#include hosts.yaml")
ts_include.Disk.strategies.AddLines(strategy_lines)

tr = Test.AddTestRun("Request via a strategy whose hosts are included from the configuration directory")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(ts_include)
tr.MakeCurlCommand(f"--verbose --proxy 127.0.0.1:{ts_include.Variables.port} http://strategy.test/path", ts=ts_include)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression("HTTP/1.1 200 OK", "The included hosts serve the request")
tr.StillRunningAfter = server
tr.StillRunningAfter = ts_include
