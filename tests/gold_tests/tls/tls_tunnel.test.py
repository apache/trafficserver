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
Test tunneling based on SNI
'''

# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=False)
server_foo = Test.MakeOriginServer("server_foo", ssl=True)
server_bar = Test.MakeOriginServer("server_bar", ssl=True)

request_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""} 
request_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_foo_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "foo ok"}
response_bar_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "bar ok"}
server_foo.addResponse("sessionlog.json", request_foo_header, response_foo_header)
server_bar.addResponse("sessionlog.json", request_bar_header, response_bar_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")

ts.Variables.ssl_port = 4443

# Need no remap rules.  Everything should be proccessed by ssl_server_name

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=signed-foo.pem ssl_key_name=signed-foo.key'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http|ssl',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.http.connect_ports': '{0} {1} {2}'.format(ts.Variables.ssl_port,server_foo.Variables.Port,server_bar.Variables.Port),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.CA.cert.filename': 'signer.pem',
    'proxy.config.url_remap.pristine_host_hdr': 1
})

# foo.com should not terminate.  Just tunnel to server_foo
# bar.com should terminate.  
# empty SNI should tunnel to server_bar
ts.Disk.ssl_server_name_yaml.AddLines([
  '- fqdn: foo.com',
  "  tunnel_route: localhost:{0}".format(server_foo.Variables.Port),
  "- fqdn: bob.*.com",
  "  tunnel_route: localhost:{0}".format(server_foo.Variables.Port),
  "- fqdn: ''", # No SNI sent
  "  tunnel_route: localhost:{0}".format(server_bar.Variables.Port)
])

tr = Test.AddTestRun("foo.com Tunnel-test")
tr.Processes.Default.StartBefore(server_foo)
tr.Processes.Default.StartBefore(server_bar)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Command = "curl -v --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Should not TLS terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("foo ok", "Should get a response from foo")

tr = Test.AddTestRun("bob.bar.com Tunnel-test")
tr.Processes.Default.Command = "curl -v --resolve 'bob.bar.com:{0}:127.0.0.1' -k  https://bob.bar.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Should not TLS terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("foo ok", "Should get a response from foo")

tr = Test.AddTestRun("bar.com no Tunnel-test")
tr.Processes.Default.Command = "curl -v --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Not Found on Accelerato", "Terminates on on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ATS", "Terminate on Traffic Server")

tr = Test.AddTestRun("no SNI Tunnel-test")
tr.Processes.Default.Command = "curl -v -k  https://127.0.0.1:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Not Found on Accelerato", "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Do not terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("bar ok", "Should get a response from bar")

# Update ssl_server_name file and reload
tr = Test.AddTestRun("Update config files")
# Update the SNI config
snipath = ts.Disk.ssl_server_name_yaml.AbsPath
recordspath = ts.Disk.records_config.AbsPath
tr.Disk.File(snipath, id = "ssl_server_name_yaml", typename="ats:config"),
tr.Disk.ssl_server_name_yaml.AddLines([
  '- fqdn: bar.com',
  "  tunnel_route: localhost:{0}".format(server_bar.Variables.Port),
])
tr.StillRunningAfter = ts
tr.StillRunningAfter = server_foo
tr.StillRunningAfter = server_bar
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'echo Updated configs'
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5

trreload = Test.AddTestRun("Reload config")
trreload.StillRunningAfter = ts
trreload.StillRunningAfter = server_foo
trreload.StillRunningAfter = server_bar
trreload.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trreload.Processes.Default.Env = ts.Env
trreload.Processes.Default.ReturnCode = 0
trreload.Processes.Default.TimeOut = 5
trreload.TimeOut = 5

# Should termimate on traffic_server (not tunnel)
tr = Test.AddTestRun("foo.com no Tunnel-test")
tr.StillRunningAfter = ts
# Wait for the reload to complete
tr.DelayStart = 5
tr.Processes.Default.Command = "curl -v --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(ts.Variables.ssl_port)
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Not Found on Accelerato", "Terminates on on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ATS", "Terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

# Should tunnel to server_bar
tr = Test.AddTestRun("bar.com  Tunnel-test")
tr.Processes.Default.Command = "curl -v --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
tr.Processes.Default.TimeOut = 5
tr.TimeOut = 5
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Not Found on Accelerato", "Terminates on on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("ATS", "Terminate on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("bar ok", "Should get a response from bar")

