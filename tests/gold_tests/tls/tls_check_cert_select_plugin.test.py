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
Test ATS offering different certificates based on SNI. Load via plugin
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", command="traffic_server", select_ports=True, enable_tls=True, dump_runroot=True)
ts.SetRunRootEnv()
server = Test.MakeOriginServer("server", ssl=True)
dns = Test.MakeDNServer("dns")

request_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed2-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")

ts.Disk.remap_config.AddLine(
    'map / https://foo.com:{1}'.format(ts.Variables.ssl_port, server.Variables.SSL_Port))

ts.Disk.ssl_multicert_config.AddLines([
    'dest_ip=127.0.0.1 ssl_cert_name=signed-foo.pem ssl_key_name=signed-foo.key',
    'ssl_cert_name=signed2-bar.pem ssl_key_name=signed-bar.key',
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
])

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_secret_load_test.so'), ts)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.diags.debug.tags': 'ssl_secret_load_test|ssl',
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.ssl.server.cert.path': '{0}/../'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}/../'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
})

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})

# Should receive a bar.com cert
tr = Test.AddTestRun("bar.com cert")
tr.Setup.Copy("ssl/signer.pem")
tr.Setup.Copy("ssl/signer2.pem")
tr.Processes.Default.Command = "curl -v --cacert ./signer2.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN=bar.com", "Cert should contain bar.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Cert should not contain foo.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")

# Should receive a foo.com cert
tr2 = Test.AddTestRun("foo.com cert")
tr2.Processes.Default.Command = "curl -v --cacert ./signer.pem --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}".format(
    ts.Variables.ssl_port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr2.Processes.Default.Streams.All += Testers.ContainsExpression("CN=foo.com", "Cert should contain foo.com")
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=bar.com", "Cert should not contain bar.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")

# Should receive random.server.com
tr2 = Test.AddTestRun("random.server.com cert")
tr2.Processes.Default.Command = "curl -v -k --resolve 'random.server.com:{0}:127.0.0.1' https://random.server.com:{0}".format(
    ts.Variables.ssl_port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr2.Processes.Default.Streams.All += Testers.ContainsExpression("CN=random.server.com", "Cert should contain random.server.com")
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Cert should not contain foo.com")
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=bar.com", "Cert should not contain bar.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")

# No SNI match should match specific IP address, foo.com
# SNI name and returned cert name will not match, so must use -k to avoid cert verification
tr2 = Test.AddTestRun("Bad SNI")
tr2.Processes.Default.Command = "curl -v -k --cacert ./signer.pem --resolve 'bad.sni.com:{0}:127.0.0.1' https://bad.sni.com:{0}".format(
    ts.Variables.ssl_port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr2.Processes.Default.Streams.All += Testers.ContainsExpression("CN=foo.com", "Cert should contain foo.com")
tr2.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=bar.com", "Cert should not contain bar.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")

# Copy in a new version of the bar.com cert.  Replace it with the version
# signed by signer 1.  Wait at least a second to sure the file update time
# differs
trupdate = Test.AddTestRun("Update server bar cert file in place")
trupdate.StillRunningAfter = ts
trupdate.StillRunningAfter = server
trupdate.Setup.CopyAs("ssl/signed-bar.pem", ".", "{0}/signed2-bar.pem".format(ts.Variables.SSLDir))
# For some reason the Setup.CopyAs does not change the modification time, so we touch
trupdate.Processes.Default.Command = 'touch {0}/signed2-bar.pem'.format(ts.Variables.SSLDir)
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trupdate.Processes.Default.Env = ts.Env
trupdate.Processes.Default.ReturnCode = 0

# The plugin will pull every 3 seconds.  So wait 4 seconds and test again.
# Request with CA=signer.pem should work.  Request with CA=signer2.pem
# should fail
tr = Test.AddTestRun("Test new version of bar cert with good CA")
tr.DelayStart = 4
tr.Processes.Default.Command = "date; curl -v --cacert ./signer.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN=bar.com", "Cert should contain bar.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Cert should not contain foo.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("404", "Should make an exchange")

tr = Test.AddTestRun("Test new version of bar cert with bad CA")
tr.Processes.Default.Command = "curl -v --cacert ./signer2.pem  --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 60
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All = Testers.ContainsExpression("unknown CA", "Failed handshake")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=bar.com", "Cert should contain bar.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("CN=foo.com", "Cert should not contain foo.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("404", "Should make an exchange")
