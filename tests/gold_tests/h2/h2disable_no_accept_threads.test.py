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
Test disabling H2 on a per domain basis
'''

# need Curl
Test.SkipUnless(
    Condition.HasCurlFeature('http2')
)

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port))

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http|ssl',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.accept_threads': 0
})

ts.Disk.sni_yaml.AddLines([
  'sni:',
  '- fqdn: bar.com',
  '  http2: off',
  '- fqdn: bob.*.com',
  '  http2: off',
])

tr = Test.AddTestRun("Negotiate-h2")
tr.Processes.Default.Command = "curl -v -k --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.TimeOut = 5
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Using HTTP2", "Curl should negotiate HTTP2")
tr.TimeOut = 5

tr2 = Test.AddTestRun("Do not negotiate h2")
tr2.Processes.Default.Command = "curl -v -k --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}".format(ts.Variables.ssl_port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.Processes.Default.TimeOut = 5
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression("Using HTTP2", "Curl should not negotiate HTTP2")
tr2.TimeOut = 5

tr2 = Test.AddTestRun("Do not negotiate h2")
tr2.Processes.Default.Command = "curl -v -k --resolve 'bob.foo.com:{0}:127.0.0.1' https://bob.foo.com:{0}".format(ts.Variables.ssl_port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.Processes.Default.TimeOut = 5
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression("Using HTTP2", "Curl should not negotiate HTTP2")
tr2.TimeOut = 5
