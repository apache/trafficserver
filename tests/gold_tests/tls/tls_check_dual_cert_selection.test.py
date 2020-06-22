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
Test ATS offering both RSA and EC certificates
'''

import os
import re

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)
dns = Test.MakeDNServer("dns")

request_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
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

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256',
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.diags.debug.tags': 'ssl',
    'proxy.config.diags.debug.enabled': 1
})

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})

foo_ec_string = ""
foo_rsa_string = ""
san_ec_string = ""
san_rsa_string = ""
with open(os.path.join(Test.TestDirectory,'ssl', 'signed-foo-ec.pem'), 'r') as myfile:
    foo_ec_string = re.escape(myfile.read())
with open(os.path.join(Test.TestDirectory,'ssl', 'signed-foo.pem'), 'r') as myfile:
    foo_rsa_string = re.escape(myfile.read())
with open(os.path.join(Test.TestDirectory,'ssl', 'signed-san-ec.pem'), 'r') as myfile:
    san_ec_string = re.escape(myfile.read())
with open(os.path.join(Test.TestDirectory,'ssl', 'signed-san.pem'), 'r') as myfile:
    san_rsa_string = re.escape(myfile.read())

# Should receive a EC cert since ATS cipher list prefers EC
tr = Test.AddTestRun("Default for foo should return EC cert")
tr.Setup.Copy("ssl/signer.pem")
tr.Processes.Default.Command = "echo foo | openssl s_client -tls1_2 -servername foo.com -connect 127.0.0.1:{0}".format(ts.Variables.ssl_port, foo_ec_string)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(foo_ec_string, "Should select EC cert",reflags=re.S | re.M)

# Should receive a RSA cert
tr = Test.AddTestRun("Only offer RSA ciphers, should receive RSA cert")
tr.Processes.Default.Command = "echo foo | openssl s_client -tls1_2 -servername foo.com -cipher 'ECDHE-RSA-AES128-GCM-SHA256' -connect 127.0.0.1:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(foo_rsa_string, "Should select RSA cert",reflags=re.S | re.M)

# Should receive a EC cert
tr = Test.AddTestRun("Default for two.com should return EC cert")
tr.Processes.Default.Command = "echo foo | openssl s_client -tls1_2 -servername two.com -connect 127.0.0.1:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(san_ec_string, "Should select EC cert", reflags=re.S | re.M)
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN");

# Should receive a RSA cert
tr = Test.AddTestRun("Only offer RSA ciphers, should receive RSA cert")
tr.Processes.Default.Command = "echo foo | openssl s_client -tls1_2 -servername two.com -cipher 'ECDHE-RSA-AES128-GCM-SHA256' -connect 127.0.0.1:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(san_rsa_string,  "Should select RSA cert", reflags=re.S | re.M)
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN");

# Should receive a RSA cert
tr = Test.AddTestRun("rsa.com only in rsa cert")
tr.Processes.Default.Command = "echo foo | openssl s_client -tls1_2 -servername rsa.com -connect 127.0.0.1:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(san_rsa_string, "Should select RSA cert", reflags=re.S | re.M)
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN");

# Should receive a EC cert
tr = Test.AddTestRun("ec.com only in ec cert")
tr.Processes.Default.Command = "echo foo | openssl s_client -tls1_2 -servername ec.com -connect 127.0.0.1:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.All += Testers.ContainsExpression(san_ec_string, "Should select EC cert", reflags=re.S | re.M)
tr.Processes.Default.Streams.All += Testers.ContainsExpression("CN = group.com", "Should select a group SAN");

