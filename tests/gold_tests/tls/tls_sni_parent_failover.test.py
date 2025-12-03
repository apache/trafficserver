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
Test parent failover with SNI name handling, validating that Traffic Server
correctly fails over between HTTPS parents (including strategy-based parents)
while enforcing TLS server name verification.
'''

# Define default ATS
ts = Test.MakeATSProcess(
    "ts",
    enable_tls=True,
    enable_cache=False,
)

server_foo = Test.MakeOriginServer(
    "server_foo",
    ssl=True,
    options={
        "--key": "{0}/server-foo.key".format(Test.RunDirectory),
        "--cert": "{0}/server-foo.pem".format(Test.RunDirectory),
    },
)
server_bar = Test.MakeOriginServer(
    "server_bar",
    ssl=True,
    options={
        "--key": "{0}/server-bar.key".format(Test.RunDirectory),
        "--cert": "{0}/server-bar.pem".format(Test.RunDirectory),
    },
)

# default check request/response
request_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_foo_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "foo ok"}
request_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_bar_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "bar ok"}

server_foo.addResponse("sessionlog.json", request_foo_header, response_foo_header)
server_bar.addResponse("sessionlog.json", request_bar_header, response_bar_header)

# successful request to be served by bar.com
request_bar_header = {"headers": "GET /path HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_bar_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "path bar ok"
}

server_bar.addResponse("sessionlog.json", request_bar_header, response_bar_header)

ts.addSSLfile("ssl/server-foo.pem")
ts.addSSLfile("ssl/server-foo.key")
ts.addSSLfile("ssl/server-bar.pem")
ts.addSSLfile("ssl/server-bar.key")

dns = Test.MakeDNServer("dns")

ts.Disk.records_config.update(
    {
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|ssl|parent_select|next_hop',
        'proxy.config.ssl.client.verify.server.policy': 'ENFORCED',
        'proxy.config.ssl.client.verify.server.properties': 'NAME',
        'proxy.config.url_remap.pristine_host_hdr': 0,
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.http.connect.down.policy': 1,  # tls failures don't mark down
    })

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})
dns.addRecords(records={"parent.": ["127.0.0.1"]})
dns.addRecords(records={"strategy.": ["127.0.0.1"]})

ts.Disk.remap_config.AddLines([
    "map http://parent https://parent",
    "map http://strategy https://strategy @strategy=strat",
])

ts.Disk.parent_config.AddLine(
    'dest_domain=. port=443 parent="foo.com:{0}|1;bar.com:{1}|1" parent_retry=simple_retry parent_is_proxy=false go_direct=false simple_server_retry_responses="404" host_override=true'
    .format(server_foo.Variables.SSL_Port, server_bar.Variables.SSL_Port))

# build strategies.yaml file
ts.Disk.File(ts.Variables.CONFIGDIR + "/strategies.yaml", id="strategies", typename="ats:config")

s = ts.Disk.strategies
s.AddLine("groups:")
s.AddLines(
    [
        f"  - &gstrat",
        f"    - host: foo.com",
        f"      protocol:",
        f"      - scheme: https",
        f"        port: {server_foo.Variables.SSL_Port}",
        f"      weight: 1.0",
        f"    - host: bar.com",
        f"      protocol:",
        f"      - scheme: https",
        f"        port: {server_bar.Variables.SSL_Port}",
        f"      weight: 1.0",
    ])

s.AddLine("strategies:")

s.AddLines(
    [
        f"  - strategy: strat",
        f"    policy: first_live",
        f"    go_direct: false",
        f"    parent_is_proxy: false",
        f"    ignore_self_detect: true",
        f"    host_override: true",
        f"    groups:",
        f"      - *gstrat",
        f"    scheme: https",
        f"    failover:",
        f"      ring_mode: exhaust_ring",
        f"      response_codes:",
        f"        - 404",
    ])

curl_args = f"-s -L -o /dev/stdout -D /dev/stderr -x localhost:{ts.Variables.port} "

tr = Test.AddTestRun("request with failover, parent.config")
tr.Setup.Copy("ssl/server-foo.key")
tr.Setup.Copy("ssl/server-foo.pem")
tr.Setup.Copy("ssl/server-bar.key")
tr.Setup.Copy("ssl/server-bar.pem")
tr.MakeCurlCommand(curl_args + "http://parent/path", ts=ts)
tr.StillRunningAfter = ts
ps = tr.Processes.Default
ps.StartBefore(server_foo)
ps.StartBefore(server_bar)
ps.StartBefore(dns)
ps.StartBefore(Test.Processes.ts)
ps.Streams.stdout = Testers.ContainsExpression("path bar ok", "Expected 200 response from bar.com")

tr = Test.AddTestRun("request with failover, strategies.yaml")
tr.MakeCurlCommand(curl_args + "http://strategy/path", ts=ts)
tr.StillRunningAfter = ts
ps = tr.Processes.Default
ps.Streams.stdout = Testers.ContainsExpression("path bar ok", "Expected 200 response from bar.com")
