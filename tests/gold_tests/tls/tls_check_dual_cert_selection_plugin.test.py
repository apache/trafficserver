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
Test ATS offering both RSA and EC certificates loaded via plugin
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)
dns = Test.MakeDNServer("dns")

request_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed2-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-foo-ec.pem")
ts.addSSLfile("ssl/signed-foo-ec.key")
ts.addSSLfile("ssl/signed-san.pem")
ts.addSSLfile("ssl/signed-san.key")
ts.addSSLfile("ssl/signed-san-ec.pem")
ts.addSSLfile("ssl/signed-san-ec.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine(
    'map / https://foo.com:{1}'.format(ts.Variables.ssl_port, server.Variables.SSL_Port))

ts.Disk.ssl_multicert_config.AddLines([
    'ssl_cert_name=signed-foo-ec.pem,signed-foo.pem ssl_key_name=signed-foo-ec.key,signed-foo.key',
    'ssl_cert_name=signed-san-ec.pem,signed-san.pem ssl_key_name=signed-san-ec.key,signed-san.key',
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
])

Test.PrepareTestPlugin(os.path.join(Test.Variables.AtsTestPluginsDir, 'ssl_secret_load_test.so'), ts)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}/../'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}/../'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.diags.debug.tags': 'ssl_secret_load_test',
    'proxy.config.diags.debug.enabled': 1
})

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})

# Should receive a EC cert
tr = Test.AddTestRun("Default for foo should return EC cert")
tr.Setup.Copy("ssl/signer.pem")
tr.Setup.Copy("ssl/signer2.pem")
tr.Processes.Default.Command = "echo foo | openssl s_client  -CAfile signer.pem -servername foo.com -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: ECDSA", "Should select EC cert")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# Should receive a RSA cert
tr = Test.AddTestRun("Only offer RSA ciphers, should receive RSA cert")
tr.Processes.Default.Command = "echo foo | openssl s_client  -CAfile signer.pem -servername foo.com -sigalgs 'RSA-PSS+SHA256' -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: RSA-PSS", "Should select RSA cert")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# Should receive a EC cert
tr = Test.AddTestRun("Default for one.com should return EC cert")
tr.Processes.Default.Command = "echo foo | openssl s_client  -CAfile signer.pem -servername one.com -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: ECDSA", "Should select EC cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# Should receive a RSA cert
tr = Test.AddTestRun("Only offer RSA ciphers, should receive RSA cert")
tr.Processes.Default.Command = "echo foo | openssl s_client  -CAfile signer.pem -servername one.com -sigalgs 'RSA-PSS+SHA256' -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: RSA-PSS", "Should select RSA cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# Should receive a RSA cert
tr = Test.AddTestRun("rsa.com only in rsa cert")
tr.Processes.Default.Command = "echo foo | openssl s_client  -CAfile signer.pem -servername rsa.com -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: RSA-PSS", "Should select RSA cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# Should receive a EC cert
tr = Test.AddTestRun("ec.com only in ec cert")
tr.Processes.Default.Command = "echo foo | openssl s_client  -CAfile signer.pem -servername ec.com -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: ECDSA", "Should select EC cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# Copy in a new version of the foo.com cert.  Replace it with the version
# signed by signer 2.  Wait at least a second to sure the file update time
# differs
trupdate = Test.AddTestRun("Update server bar cert file in place")
trupdate.StillRunningAfter = ts
trupdate.StillRunningAfter = server
trupdate.Setup.CopyAs("ssl/signed2-foo.pem", ".", "{0}/signed-foo.pem".format(ts.Variables.SSLDir))
# For some reason the Setup.CopyAs does not change the modification time, so we touch
trupdate.Processes.Default.Command = 'touch {0}/signed-foo.pem'.format(ts.Variables.SSLDir)
# Need to copy over the environment so traffic_ctl knows where to find the unix domain socket
trupdate.Processes.Default.Env = ts.Env
trupdate.Processes.Default.ReturnCode = 0

# The plugin will pull every 3 seconds.  So wait 4 seconds and test again.  Request with CA=signer2.pem should work.  Request with CA=signer.pem should fail
# Should receive a RSA cert
tr = Test.AddTestRun("Only offer RSA ciphers, should receive RSA cert")
tr.Processes.Default.Command = "echo foo | openssl s_client -CAfile signer.pem  -servername foo.com -sigalgs 'RSA-PSS+SHA256' -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.DelayStart = 4
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: RSA-PSS", "Should select RSA cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = foo.com", "Should select foo.com")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("unable to verify the first certificate", "Different signer")

tr = Test.AddTestRun("Only offer RSA ciphers, should receive RSA cert with correct CA")
tr.Processes.Default.Command = "echo foo | openssl s_client -CAfile signer2.pem  -servername foo.com -sigalgs 'RSA-PSS+SHA256' -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: RSA-PSS", "Should select RSA cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = foo.com", "Should select foo.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")

# The EC case should be unchanged
tr = Test.AddTestRun("Offer any cipher")
tr.Processes.Default.Command = "echo foo | openssl s_client -CAfile signer.pem  -servername foo.com  -connect 127.0.0.1:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression("Peer signature type: ECDSA", "Should select EC cert")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = foo.com", "Should select foo.com")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("unable to verify the first certificate", "Correct signer")
