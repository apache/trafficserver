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
Test conf_remp to specify different client certificates to offer to the origin.  Loading certs/keys via plugin.
'''


ts = Test.MakeATSProcess("ts", command="traffic_server", select_ports=True, dump_runroot=True)
ts.SetRunRootEnv()
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
server3 = Test.MakeOriginServer("server3")
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
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {
    "headers": "HTTP/1.1 200 OK\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed2-foo.pem")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed2-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")

ts.Disk.sni_yaml.AddLine('sni:')
ts.Disk.sni_yaml.AddLine('- fqdn: random')
ts.Disk.sni_yaml.AddLine('  verify_server_properties: NONE')
snipath = ts.Disk.sni_yaml.AbsPath

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_secret_load_test.so'), ts)

shortdir = ts.Variables.SSLDir[0:ts.Variables.SSLDir.rfind("/")]

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    'proxy.config.ssl.client.cert.path': '{0}'.format(shortdir),
    'proxy.config.ssl.client.cert.filename': 'signed-foo.pem',
    'proxy.config.ssl.client.private_key.path': '{0}'.format(shortdir),
    'proxy.config.ssl.client.private_key.filename': 'signed-foo.key',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl_secret_load|http|ssl',
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map /case1 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename={1} plugin=conf_remap.so @pparam=proxy.config.ssl.client.private_key.filename={2}'.format(
        server.Variables.SSL_Port,
        "signed-foo.pem",
        "signed-foo.key"))
ts.Disk.remap_config.AddLine(
    'map /badcase1 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename={1} plugin=conf_remap.so @pparam=proxy.config.ssl.client.private_key.filename={2}'.format(
        server.Variables.SSL_Port,
        "signed2-foo.pem",
        "signed-foo.key"))
ts.Disk.remap_config.AddLine(
    'map /case2 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename={1} plugin=conf_remap.so @pparam=proxy.config.ssl.client.private_key.filename={2}'.format(
        server2.Variables.SSL_Port,
        "signed2-foo.pem",
        "signed-foo.key"))
ts.Disk.remap_config.AddLine(
    'map /badcase2 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename={1} plugin=conf_remap.so @pparam=proxy.config.ssl.client.private_key.filename={2}'.format(
        server2.Variables.SSL_Port,
        "signed-foo.pem",
        "signed-foo.key"))

# Should succeed
tr = Test.AddTestRun("Connect with correct client cert to first server")
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(server2)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = "curl -H host:example.com  http://127.0.0.1:{0}/case1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

# Should fail
trfail = Test.AddTestRun("Connect with bad client cert to first server")
trfail.StillRunningAfter = ts
trfail.StillRunningAfter = server
trfail.StillRunningAfter = server2
trfail.Processes.Default.Command = 'curl -H host:example.com  http://127.0.0.1:{0}/badcase1'.format(ts.Variables.port)
trfail.Processes.Default.ReturnCode = 0
trfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Should succeed
trbar = Test.AddTestRun("Connect with correct client cert to second server")
trbar.StillRunningAfter = ts
trbar.StillRunningAfter = server
trbar.StillRunningAfter = server2
trbar.Processes.Default.Command = "curl -H host:bar.com  http://127.0.0.1:{0}/case2".format(ts.Variables.port)
trbar.Processes.Default.ReturnCode = 0
trbar.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

# Should fail
trbarfail = Test.AddTestRun("Connect with bad client cert to second server")
trbarfail.StillRunningAfter = ts
trbarfail.StillRunningAfter = server
trbarfail.StillRunningAfter = server2
trbarfail.Processes.Default.Command = 'curl -H host:bar.com  http://127.0.0.1:{0}/badcase2'.format(ts.Variables.port)
trbarfail.Processes.Default.ReturnCode = 0
trbarfail.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

# Test the case of updating certificate contents without changing file name.
trupdate = Test.AddTestRun("Update client cert file in place")
trupdate.StillRunningAfter = ts
trupdate.StillRunningAfter = server
trupdate.StillRunningAfter = server2
# in the config/ssl directory for records.config
trupdate.Setup.CopyAs("ssl/signed-foo.pem", ".", "{0}/signed2-foo.pem".format(ts.Variables.SSLDir))
trupdate.Processes.Default.Command = 'traffic_ctl config set proxy.config.ssl.client.cert.path {0}/; touch {1}'.format(
    shortdir, snipath)
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trupdate.Processes.Default.Env = ts.Env
trupdate.Processes.Default.ReturnCode = 0

tr2reload = Test.AddTestRun("Reload config")
tr2reload.StillRunningAfter = ts
tr2reload.StillRunningAfter = server
tr2reload.StillRunningAfter = server2
tr2reload.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr2reload.Processes.Default.Env = ts.Env
tr2reload.Processes.Default.ReturnCode = 0

tr3bar = Test.AddTestRun("Make request with other foo.  badcase1 should now work")
# Wait for the reload to complete
tr3bar.Processes.Default.StartBefore(server3, ready=When.FileContains(ts.Disk.diags_log.Name, 'sni.yaml finished loading', 3))
tr3bar.StillRunningAfter = ts
tr3bar.StillRunningAfter = server
tr3bar.StillRunningAfter = server2
tr3bar.Processes.Default.Command = 'curl  -H host:foo.com http://127.0.0.1:{0}/badcase1'.format(ts.Variables.port)
tr3bar.Processes.Default.ReturnCode = 0
tr3bar.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")

# Test the hot-reload feature.  Update cert file and wait about.  Should update without reload
trupdate = Test.AddTestRun("Update signed-foo cert file in place")
trupdate.StillRunningAfter = ts
trupdate.StillRunningAfter = server
trupdate.Setup.CopyAs("ssl/signed2-foo.pem", ".", "{0}/signed-foo.pem".format(ts.Variables.SSLDir))
# For some reason the Setup.CopyAs does not change the modification time, so we touch
trupdate.Processes.Default.Command = 'touch {0}/signed-foo.pem {0}/signed-foo.key'.format(ts.Variables.SSLDir)
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trupdate.Processes.Default.Env = ts.Env
trupdate.Processes.Default.ReturnCode = 0

# The plugin will pull every 3 seconds.  So wait 4 seconds and test again.
# case1 should fail
# badcase1 should succeed
tr = Test.AddTestRun("Retest case1")
tr.DelayStart = 4
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.StillRunningAfter = ts
tr.Processes.Default.Command = "curl -H host:example.com  http://127.0.0.1:{0}/case1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Check response")

tr = Test.AddTestRun("Retest badcase1")
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.StillRunningAfter = ts
tr.Processes.Default.Command = "curl -H host:example.com  http://127.0.0.1:{0}/badcase1".format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check response")
