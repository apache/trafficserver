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
Test different combinations of TLS handshake hooks to ensure they are applied consistently.
'''

Test.SkipUnless(Condition.HasProgram("grep", "grep needs to be installed on system for this test to work"))

ts = Test.MakeATSProcess("ts", command="traffic_manager", select_ports=False)
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
    'proxy.config.http.server_ports': '{0}'.format(ts.Variables.port),
    'proxy.config.ssl.client.verify.server':  0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.ssl.client.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.cert.filename': 'signed-foo.pem',
    'proxy.config.ssl.client.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.private_key.filename': 'signed-foo.key',
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

ts.Disk.ssl_server_name_yaml.AddLine(
    '- fqdn: bar.com')
ts.Disk.ssl_server_name_yaml.AddLine(
    '  client_cert: {0}/signed2-bar.pem'.format(ts.Variables.SSLDir))
ts.Disk.ssl_server_name_yaml.AddLine(
    '  client_key: {0}/signed-bar.key'.format(ts.Variables.SSLDir))


# Should succeed
tr = Test.AddTestRun("Connect with first client cert to first server")
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(server2)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:example.com  http://127.0.0.1:{0}/case1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 5
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr.TimeOut = 5

#Should fail
trfail = Test.AddTestRun("Connect with first client cert to second server")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:example.com  http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.TimeOut = 5
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")
trfail.TimeOut = 5

# Should succeed
trbar = Test.AddTestRun("Connect with signed2 bar to second server")
trbar.StillRunningAfter = ts
trbar.StillRunningAfter = server
trbar.StillRunningAfter = server2
trbar.Processes.Default.Command = "curl -H host:bar.com  http://127.0.0.1:{0}/case2".format(ts.Variables.port)
trbar.Processes.Default.ReturnCode = 0
trbar.Processes.Default.TimeOut = 5
trbar.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
trbar.TimeOut = 5

#Should fail
trbarfail = Test.AddTestRun("Connect with signed2 bar cert to first server")
trbarfail.StillRunningAfter = ts
trbarfail.StillRunningAfter = server
trbarfail.StillRunningAfter = server2
trbarfail.Processes.Default.Command = 'curl -H host:bar.com  http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
trbarfail.Processes.Default.ReturnCode = 0
trbarfail.Processes.Default.TimeOut = 5
trbarfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")
trbarfail.TimeOut = 5

tr2 = Test.AddTestRun("Update config files")
# Update the SNI config
snipath = ts.Disk.ssl_server_name_yaml.AbsPath
recordspath = ts.Disk.records_config.AbsPath
tr2.Disk.File(snipath, id = "ssl_server_name_yaml", typename="ats:config"),
tr2.Disk.ssl_server_name_yaml.AddLine(
    '- fqdn: bar.com')
tr2.Disk.ssl_server_name_yaml.AddLine(
    '  client_cert: {0}/signed-bar.pem'.format(ts.Variables.SSLDir))
tr2.Disk.ssl_server_name_yaml.AddLine(
    '  client_key: {0}/signed-bar.key'.format(ts.Variables.SSLDir))
# recreate the records.config with the cert filename changed
tr2.Disk.File(recordspath, id = "records_config", typename="ats:config:records"),
tr2.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl|http',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': '{0}'.format(ts.Variables.port),
    'proxy.config.ssl.client.verify.server':  0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.ssl.client.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.cert.filename': 'signed2-foo.pem',
    'proxy.config.ssl.client.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.private_key.filename': 'signed-foo.key',
    'proxy.config.url_remap.pristine_host_hdr' : 1,
})
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.StillRunningAfter = server2
tr2.Processes.Default.Command = 'echo Updated configs'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr2.Processes.Default.Env = ts.Env
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.TimeOut = 5
tr2.TimeOut = 5

tr2reload = Test.AddTestRun("Reload config")
tr2reload.StillRunningAfter = ts
tr2reload.StillRunningAfter = server
tr2reload.StillRunningAfter = server2
tr2reload.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr2reload.Processes.Default.Env = ts.Env
tr2reload.Processes.Default.ReturnCode = 0
tr2reload.Processes.Default.TimeOut = 5
tr2reload.TimeOut = 5


#Should succeed
tr3bar = Test.AddTestRun("Make request with other bar cert to first server")
# Wait for the reload to complete
tr3bar.DelayStart = 10
tr3bar.StillRunningAfter = ts
tr3bar.StillRunningAfter = server
tr3bar.StillRunningAfter = server2
tr3bar.Processes.Default.Command = 'curl  -H host:bar.com http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
tr3bar.Processes.Default.ReturnCode = 0
tr3bar.Processes.Default.TimeOut = 5
tr3bar.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr3bar.TimeOut = 5

#Should fail
tr3barfail = Test.AddTestRun("Make request with other bar cert to second server")
tr3barfail.StillRunningAfter = ts
tr3barfail.StillRunningAfter = server
tr3barfail.StillRunningAfter = server2
tr3barfail.Processes.Default.Command = 'curl  -H host:bar.com http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
tr3barfail.Processes.Default.ReturnCode = 0
tr3barfail.Processes.Default.TimeOut = 5
tr3barfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")
tr3barfail.TimeOut = 5

#Should succeed
tr3 = Test.AddTestRun("Make request with other cert to second server")
# Wait for the reload to complete
tr3.StillRunningAfter = ts
tr3.StillRunningAfter = server
tr3.StillRunningAfter = server2
tr3.Processes.Default.Command = 'curl  -H host:example.com http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.TimeOut = 5
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr3.TimeOut = 5

#Should fail
tr3fail = Test.AddTestRun("Make request with other cert to first server")
tr3fail.StillRunningAfter = ts
tr3fail.StillRunningAfter = server
tr3fail.StillRunningAfter = server2
tr3fail.Processes.Default.Command = 'curl  -H host:example.com http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
tr3fail.Processes.Default.ReturnCode = 0
tr3fail.Processes.Default.TimeOut = 5
tr3fail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")
tr3fail.TimeOut = 5


# Test the case of updating certificate contents without changing file name.
trupdate = Test.AddTestRun("Update client cert file in place")
trupdate.StillRunningAfter = ts
trupdate.StillRunningAfter = server
trupdate.StillRunningAfter = server2
# Make a meaningless config change on the path so the records.config reload logic will trigger
trupdate.Setup.CopyAs("ssl/signed2-bar.pem", ".", "{0}/signed-bar.pem".format(ts.Variables.SSLDir))
# in the config/ssl directory for records.config
trupdate.Setup.CopyAs("ssl/signed-foo.pem", ".", "{0}/signed2-foo.pem".format(ts.Variables.SSLDir))
trupdate.Processes.Default.Command = 'traffic_ctl config set proxy.config.ssl.client.cert.path {0}/; touch {1}'.format(ts.Variables.SSLDir,snipath)
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trupdate.Processes.Default.Env = ts.Env
trupdate.Processes.Default.ReturnCode = 0
trupdate.Processes.Default.TimeOut = 5

trreload = Test.AddTestRun("Reload config after renaming certs")
trreload.StillRunningAfter = ts
trreload.StillRunningAfter = server
trreload.StillRunningAfter = server2
trreload.Processes.Default.Command = 'traffic_ctl config reload'
trreload.Processes.Default.Env = ts.Env
trreload.Processes.Default.ReturnCode = 0
trreload.Processes.Default.TimeOut = 5

#Should succeed
tr4bar = Test.AddTestRun("Make request with renamed bar cert to second server")
# Wait for the reload to complete
tr4bar.DelayStart = 10
tr4bar.StillRunningAfter = ts
tr4bar.StillRunningAfter = server
tr4bar.StillRunningAfter = server2
tr4bar.Processes.Default.Command = 'curl  -H host:bar.com http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
tr4bar.Processes.Default.ReturnCode = 0
tr4bar.Processes.Default.TimeOut = 5
tr4bar.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr4bar.TimeOut = 5

#Should fail
tr4barfail = Test.AddTestRun("Make request with renamed bar cert to first server")
tr4barfail.StillRunningAfter = ts
tr4barfail.StillRunningAfter = server
tr4barfail.StillRunningAfter = server2
tr4barfail.Processes.Default.Command = 'curl  -H host:bar.com http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
tr4barfail.Processes.Default.ReturnCode = 0
tr4barfail.Processes.Default.TimeOut = 5
tr4barfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

#Should succeed
tr4 = Test.AddTestRun("Make request with renamed foo cert to first server")
tr4.StillRunningAfter = ts
tr4.StillRunningAfter = server
tr4.StillRunningAfter = server2
tr4.Processes.Default.Command = 'curl  -H host:example.com http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
tr4.Processes.Default.ReturnCode = 0
tr4.Processes.Default.TimeOut = 5
tr4.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
tr4.TimeOut = 5

#Should fail
tr4fail = Test.AddTestRun("Make request with renamed foo cert to second server")
tr4fail.StillRunningAfter = ts
tr4fail.StillRunningAfter = server
tr4fail.StillRunningAfter = server2
tr4fail.Processes.Default.Command = 'curl  -H host:example.com http://127.0.0.1:{0}/case2'.format(ts.Variables.port)
tr4fail.Processes.Default.ReturnCode = 0
tr4fail.Processes.Default.TimeOut = 5
tr4fail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")
tr4fail.TimeOut = 5

