'''
Test offering client cert to origin, but using plugin for cert loading
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

import subprocess
import os

Test.Summary = '''
Test offering client cert to origin, but using plugin for cert loading
'''

ts = Test.MakeATSProcess("ts", command="traffic_server", select_ports=True)
cafile = "{0}/signer.pem".format(Test.RunDirectory)
cafile2 = "{0}/signer2.pem".format(Test.RunDirectory)
server = Test.MakeOriginServer("server",
                               ssl=True,
                               options={"--clientCA": cafile,
                                        "--clientverify": ""},
                               clientcert="{0}/signed-foo.pem".format(Test.RunDirectory),
                               clientkey="{0}/signed-foo.key".format(Test.RunDirectory))
server2 = Test.MakeOriginServer("server2",
                                ssl=True,
                                options={"--clientCA": cafile2,
                                         "--clientverify": ""},
                                clientcert="{0}/signed2-bar.pem".format(Test.RunDirectory),
                                clientkey="{0}/signed-bar.key".format(Test.RunDirectory))
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

#
# Certs and keys loaded into the ts/ssl directory, but the paths in the
# configs omit the ssl subdirectory.  The ssl_secret_load_test plugin adds this back in
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/combo-signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed2-foo.pem")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed2-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_secret_load_test.so'), ts)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl_secret_load_test',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.cert.path': '{0}/../'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.url_remap.pristine_host_hdr': 1,
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map /case1 https://127.0.0.1:{0}/'.format(server.Variables.SSL_Port)
)
ts.Disk.remap_config.AddLine(
    'map /case2 https://127.0.0.1:{0}/'.format(server2.Variables.SSL_Port)
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bob.bar.com',
    '  client_cert: {0}/../signed-bar.pem'.format(ts.Variables.SSLDir),
    '  client_key: {0}/../signed-bar.key'.format(ts.Variables.SSLDir),
    '- fqdn: bob.*.com',
    '  client_cert: {0}/../combo-signed-foo.pem'.format(ts.Variables.SSLDir),
    '- fqdn: "*bar.com"',
    '  client_cert: {0}/../signed2-bar.pem'.format(ts.Variables.SSLDir),
    '  client_key: {0}/../signed-bar.key'.format(ts.Variables.SSLDir),
    '- fqdn: "foo.com"',
    '  client_cert: {0}/../signed2-foo.pem'.format(ts.Variables.SSLDir),
    '  client_key: {0}/../signed-foo.key'.format(ts.Variables.SSLDir),
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
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

# Should fail
trfail = Test.AddTestRun("bob.bar.com to server 2")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:bob.bar.com  http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should succeed
tr = Test.AddTestRun("bob.foo.com to server 1")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:bob.foo.com  http://127.0.0.1:{0}/case1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

# Should fail
trfail = Test.AddTestRun("bob.foo.com to server 2")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:bob.foo.com  http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should succeed
tr = Test.AddTestRun("random.bar.com to server 2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:random.bar.com  http://127.0.0.1:{0}/case2".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

# Should fail
trfail = Test.AddTestRun("random.bar.com to server 1")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:random.bar.com  http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should fail
tr = Test.AddTestRun("random.foo.com to server 2")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:random.foo.com  http://127.0.0.1:{0}/case2".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should fail
trfail = Test.AddTestRun("random.foo.com to server 1")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:random.foo.com  http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")
