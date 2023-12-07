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

Test.Summary = '''
Test ATS offering different certificates based on SNI
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager", enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)
server3 = Test.MakeOriginServer("server3", ssl=True)

request_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed2-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")
ts.addSSLfile("ssl/combo.pem")

ts.Disk.remap_config.AddLine('map /stuff https://foo.com:{1}'.format(ts.Variables.ssl_port, server.Variables.SSL_Port))

ts.Disk.ssl_multicert_config.AddLines(
    [
        'ssl_cert_name=signed-bar.pem ssl_key_name=signed-bar.key',
        'dest_ip=* ssl_cert_name=combo.pem',
    ])

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.url_remap.pristine_host_hdr': 1,
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
        'proxy.config.diags.debug.tags': 'ssl|http|lm',
        'proxy.config.diags.debug.enabled': 1
    })

# Should receive a bar.com cert issued by first signer
tr = Test.AddTestRun("bar.com cert signer1")
tr.Setup.Copy("ssl/signer.pem")
tr.Setup.Copy("ssl/signer2.pem")
tr.Processes.Default.Command = "curl -v --cacert ./signer.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/random".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN=bar.com", "Cert should contain bar.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Cert should not contain foo.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")

tr = Test.AddTestRun("bar.com cert signer2")
tr.Processes.Default.Command = "curl -v --cacert ./signer2.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/random".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 60
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "unable to get local issuer certificate", "Server certificate not issued by expected signer")

# Pause a little to ensure mtime will be updated
tr = Test.AddTestRun("Pause a little to ensure mtime will be different")
tr.Processes.Default.Command = "sleep 2"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Update the bar cert to the signed 2 version")
tr.Setup.CopyAs("ssl/signed2-bar.pem", ".", "{0}/signed-bar.pem".format(ts.Variables.SSLDir))
# For some reason the Setup.CopyAs does not change the modification time, so we touch
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = 'touch {0}/signed-bar.pem'.format(ts.Variables.SSLDir)
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Reload config")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = 'traffic_ctl config reload'
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Try with signer 1 again")
# Wait for the reload to complete
tr.Processes.Default.StartBefore(
    server3, ready=When.FileContains(ts.Disk.diags_log.Name, 'ssl_multicert.config finished loading', 2))
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --cacert ./signer.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/random".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 60
tr.Processes.Default.Streams.All = Testers.ContainsExpression(
    "unable to get local issuer certificate", "Server certificate not issued by expected signer")

tr = Test.AddTestRun("Try with signer 2 again")
tr.Processes.Default.Command = "curl -v --cacert ./signer2.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/random".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN=bar.com", "Cert should contain bar.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Cert should not contain foo.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")
