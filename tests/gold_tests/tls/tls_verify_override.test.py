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
Test tls server certificate verification options. Exercise conf_remap
'''

# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=False)
server_foo = Test.MakeOriginServer("server_foo", ssl=True, options = {"--key": "{0}/signed-foo.key".format(Test.RunDirectory), "--cert": "{0}/signed-foo.pem".format(Test.RunDirectory)})
server_bar = Test.MakeOriginServer("server_bar", ssl=True, options = {"--key": "{0}/signed-bar.key".format(Test.RunDirectory), "--cert": "{0}/signed-bar.pem".format(Test.RunDirectory)})
server = Test.MakeOriginServer("server", ssl=True)

dns = Test.MakeDNServer("dns")

request_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bad_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: bad_foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bad_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bad_bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server_foo.addResponse("sessionlog.json", request_foo_header, response_header)
server_foo.addResponse("sessionlog.json", request_bad_foo_header, response_header)
server_bar.addResponse("sessionlog.json", request_bar_header, response_header)
server_bar.addResponse("sessionlog.json", request_bad_bar_header, response_header)

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
ts.Disk.remap_config.AddLine(
    'map http://foo.com/basictobar https://bar.com:{0}'.format(server_bar.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://foo.com/basic https://foo.com:{0}'.format(server_foo.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://foo.com/override https://foo.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED'.format(server_foo.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://bar.com/basic https://bar.com:{0}'.format(server_foo.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://bar.com/overridedisabled https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=DISABLED'.format(server_foo.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://bar.com/overridesignature https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=SIGNATURE @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED'.format(server_foo.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://bar.com/overrideenforced https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED'.format(server_foo.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /basic https://random.com:{0}'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /overrideenforce https://127.0.0.1:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /overridename  https://127.0.0.1:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=NAME'.format(server.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /snipolicyfooremap  https://foo.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=NAME @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED @plugin=conf_remap.so @pparam=proxy.config.ssl.client.sni_policy=remap'.format(server_bar.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /snipolicyfoohost  https://foo.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=NAME @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED @plugin=conf_remap.so @pparam=proxy.config.ssl.client.sni_policy=host'.format(server_bar.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /snipolicybarremap  https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=NAME @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED @plugin=conf_remap.so @pparam=proxy.config.ssl.client.sni_policy=remap'.format(server_bar.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map /snipolicybarhost  https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=NAME @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED @plugin=conf_remap.so @pparam=proxy.config.ssl.client.sni_policy=host'.format(server_bar.Variables.Port))

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    # set global policy
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    'proxy.config.ssl.client.verify.server.properties': 'ALL',
    'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.CA.cert.filename': 'signer.pem',
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.client.sni_policy': 'remap'
})

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})
dns.addRecords(records={"random.com.": ["127.0.0.1"]})

# Should succeed without message
tr = Test.AddTestRun("default-permissive-success")
tr.Setup.Copy("ssl/signed-foo.key")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.Setup.Copy("ssl/signed-bar.pem")
tr.Processes.Default.Command = 'curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/basic'.format(ts.Variables.port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(server_foo)
tr.Processes.Default.StartBefore(server_bar)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
# Should succed.  No message
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr2 = Test.AddTestRun("default-permissive-fail")
tr2.Processes.Default.Command = "curl -k -H \"host: bar.com\"  http://127.0.0.1:{0}/basic".format(ts.Variables.port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts
# Should succeed, but will be message in log about name mismatch
tr2.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr2 = Test.AddTestRun("default-permissive-fail2")
tr2.Processes.Default.Command = "curl -k -H \"host: random.com\"  http://127.0.0.1:{0}/basic".format(ts.Variables.port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts
# Should succeed, but will be message in log about signature 
tr2.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr3 = Test.AddTestRun("default-foo-to-bar")
tr3.Processes.Default.Command = "curl -k -v -H \"host: foo.com\"  http://127.0.0.1:{0}/basictobar".format(ts.Variables.port)
tr3.ReturnCode = 0
tr3.StillRunningAfter = server
tr3.StillRunningAfter = ts
# Should succeed.  No error messages
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr3 = Test.AddTestRun("override-foo")
tr3.Processes.Default.Command = "curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/override".format(ts.Variables.port)
tr3.ReturnCode = 0
tr3.StillRunningAfter = server
tr3.StillRunningAfter = ts
# Should succeed.  No error messages
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr4 = Test.AddTestRun("override-bar-disabled")
tr4.Processes.Default.Command = "curl -k -H \"host: bad_bar.com\"  http://127.0.0.1:{0}/overridedisabled".format(ts.Variables.port)
tr4.ReturnCode = 0
tr4.StillRunningAfter = server
tr4.StillRunningAfter = ts
# Succeed. No error messages
tr4.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr5 = Test.AddTestRun("override-bar-signature-enforced")
tr5.Processes.Default.Command = "curl -k -H \"host: bar.com\"  http://127.0.0.1:{0}/overridesignature".format(ts.Variables.port)
tr5.ReturnCode = 0
tr5.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr5.StillRunningAfter = server
tr5.StillRunningAfter = ts

tr6 = Test.AddTestRun("override-bar-enforced")
tr6.Processes.Default.Command = "curl -k -H \"host: bar.com\"  http://127.0.0.1:{0}/overrideenforced".format(ts.Variables.port)
tr6.ReturnCode = 0
# Should fail
tr6.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Curl attempt should have failed")
tr6.StillRunningAfter = server
tr6.StillRunningAfter = ts

# Should succeed
tr = Test.AddTestRun("foo-to-bar-sni-policy-remap")
tr.Processes.Default.Command = "curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/snipolicybarremap".format(ts.Variables.port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could not connect", "Curl attempt should succeed")

# Should fail
tr = Test.AddTestRun("foo-to-bar-sni-policy-host")
tr.Processes.Default.Command = "curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/snipolicybarhost".format(ts.Variables.port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could not connect", "Curl attempt should fail")

# Should fail
tr = Test.AddTestRun("bar-to-foo-sni-policy-remap")
tr.Processes.Default.Command = "curl -k -H \"host: bar.com\"  http://127.0.0.1:{0}/snipolicyfooremap".format(ts.Variables.port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could not connect", "Curl attempt should fail")

# Should succeed
tr = Test.AddTestRun("bar-to-foo-sni-policy-host")
tr.Processes.Default.Command = "curl -k -H \"host: bar.com\"  http://127.0.0.1:{0}/snipolicyfoohost".format(ts.Variables.port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could not connect", "Curl attempt should succeed")

# Over riding the built in ERROR check since we expect some cases to fail

# checks on random.com should fail with message only
ts.Disk.diags_log.Content = Testers.ContainsExpression("WARNING: Core server certificate verification failed for \(random.com\). Action=Continue Error=self signed certificate server=random.com\(127.0.0.1\) depth=0", "Warning for self signed certificate")
# permissive failure for bar.com
ts.Disk.diags_log.Content += Testers.ContainsExpression("WARNING: SNI \(bar.com\) not in certificate. Action=Continue server=bar.com\(127.0.0.1\)", "Warning on missing name for bar.com")
# name check failure for random.com
ts.Disk.diags_log.Content += Testers.ContainsExpression("WARNING: SNI \(random.com\) not in certificate. Action=Continue server=random.com\(127.0.0.1\)", "Warning on missing name for randome.com")
# name check failure for bar.com
ts.Disk.diags_log.Content += Testers.ContainsExpression("WARNING: SNI \(bar.com\) not in certificate. Action=Terminate server=bar.com\(127.0.0.1\)", "Failure on missing name for bar.com")
# See if the explicitly set default sni_policy of remap works.  
ts.Disk.diags_log.Content += Testers.ExcludesExpression("WARNING: SNI \(foo.com\) not in certificate. Action=Continue", "Warning on missing name for foo.com")


