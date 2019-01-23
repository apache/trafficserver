'''
Test offering client cert to origin
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
import re

Test.Summary = '''
Test client certs to origin selected via wildcard names in ssl_server_name
'''

Test.SkipUnless(Condition.HasProgram("grep", "grep needs to be installed on system for this test to work"))

ts = Test.MakeATSProcess("ts", command="traffic_server", select_ports=False)
cafile = "{0}/signer.pem".format(Test.RunDirectory)
cafile2 = "{0}/signer2.pem".format(Test.RunDirectory)
server = Test.MakeOriginServer("server", ssl=True, options = { "--clientCA": cafile, "--clientverify": "true"}, clientcert="{0}/signed-foo.pem".format(Test.RunDirectory), clientkey="{0}/signed-foo.key".format(Test.RunDirectory))
server2 = Test.MakeOriginServer("server2", ssl=True, options = { "--clientCA": cafile2, "--clientverify": "true"}, clientcert="{0}/signed2-bar.pem".format(Test.RunDirectory), clientkey="{0}/signed-bar.key".format(Test.RunDirectory))
server.Setup.Copy("ssl/signer.pem")
server.Setup.Copy("ssl/signer2.pem")
server.Setup.Copy("ssl/signed-foo.pem")
server.Setup.Copy("ssl/signed-foo.key")
server.Setup.Copy("ssl/signed2-foo.pem")
server.Setup.Copy("ssl/signed2-bar.pem")
server.Setup.Copy("ssl/signed-bar.key")
server2.Setup.Copy("ssl/signer.pem")
server2.Setup.Copy("ssl/signer2.pem")
server2.Setup.Copy("ssl/signed-foo.pem")
server2.Setup.Copy("ssl/signed-foo.key")
server2.Setup.Copy("ssl/signed2-foo.pem")
server2.Setup.Copy("ssl/signed2-bar.pem")
server2.Setup.Copy("ssl/signed-bar.key")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed2-foo.pem")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed2-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")

ts.Variables.ssl_port = 4443
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl_verify_test',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': '{0}'.format(ts.Variables.port),
    'proxy.config.ssl.client.verify.server':  0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.url_remap.pristine_host_hdr' : 1,
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map /case1 https://127.0.0.1:{0}/'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map /case2 https://127.0.0.1:{0}/'.format(server2.Variables.Port)
)

ts.Disk.ssl_server_name_yaml.AddLines([
    '- fqdn: bob.bar.com',
    '  client_cert: signed-bar.pem',
    '  client_key: signed-bar.key',
    '- fqdn: bob.*.com',
    '  client_cert: {0}/signed-foo.pem'.format(ts.Variables.SSLDir),
    '  client_key: {0}/signed-foo.key'.format(ts.Variables.SSLDir),
    '- fqdn: "*bar.com"',
    '  client_cert: {0}/signed2-bar.pem'.format(ts.Variables.SSLDir),
    '  client_key: {0}/signed-bar.key'.format(ts.Variables.SSLDir),
])


# Should succeed
tr = Test.AddTestRun("bob.bar.com to server 1")
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(server2)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:bob.bar.com  http://127.0.0.1:{0}/case1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 10
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

#Should fail
trfail = Test.AddTestRun("bob.bar.com to server 2")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:bob.bar.com  http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.TimeOut = 10
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should succeed
tr = Test.AddTestRun("bob.foo.com to server 1")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:bob.foo.com  http://127.0.0.1:{0}/case1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 10
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

#Should fail
trfail = Test.AddTestRun("bob.foo.com to server 2")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:bob.foo.com  http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.TimeOut = 10
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should succeed
tr = Test.AddTestRun("random.bar.com to server 2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:random.bar.com  http://127.0.0.1:{0}/case2".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 10
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

#Should fail
trfail = Test.AddTestRun("random.bar.com to server 1")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:random.bar.com  http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.TimeOut = 10
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

