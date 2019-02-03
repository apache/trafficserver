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
Test TLS protocol offering  based on SNI
'''

# By default only offer TLSv1_2
# for special doman foo.com only offer TLSv1 and TLSv1_1

# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work"),
    Condition.HasOpenSSLVersion("1.1.1")
)

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server = Test.MakeOriginServer("server", ssl=True)

request_foo_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""} 
response_foo_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "foo ok"}
server.addResponse("sessionlog.json", request_foo_header, response_foo_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Variables.ssl_port = 4443

# Need no remap rules.  Everything should be proccessed by ssl_server_name

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.TLSv1': 0,
    'proxy.config.ssl.TLSv1_1': 0,
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.TLSv1_2': 1
})

# foo.com should only offer the older TLS protocols
# bar.com should terminate.  
# empty SNI should tunnel to server_bar
ts.Disk.ssl_server_name_yaml.AddLines([
  '- fqdn: foo.com',
  '  valid_tls_versions_in: [ TLSv1, TLSv1_1 ]'
])

# Target foo.com for TLSv1_2.  Should fail
tr = Test.AddTestRun("foo.com TLSv1_2")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Command = "curl -v --tls-max 1.2 --tlsv1.2 --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 35 
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ssl_choose_client_version:unsupported protocol", "Should not allow TLSv1_2")

# Target foo.com for TLSv1.  Should succeed
tr = Test.AddTestRun("foo.com TLSv1")
tr.Processes.Default.Command = "curl -v --tls-max 1.0 --tlsv1 --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts

# Target bar.com for TLSv1.  Should fail
tr = Test.AddTestRun("bar.com TLSv1")
tr.Processes.Default.Command = "curl -v --tls-max 1.0 --tlsv1 --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 35 
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("alert protocol version", "Should not allow TLSv1_0")

# Target bar.com for TLSv1_2.  Should succeed
tr = Test.AddTestRun("bar.com TLSv1_2")
tr.Processes.Default.Command = "curl -v --tls-max 1.2 --tlsv1.2 --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = ts

