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
Test tls server certificate verification without a pristine host header
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server_foo = Test.MakeOriginServer(
    "server_foo",
    ssl=True,
    options={
        "--key": "{0}/signed-foo.key".format(Test.RunDirectory),
        "--cert": "{0}/signed-foo.pem".format(Test.RunDirectory)
    })
server = Test.MakeOriginServer("server", ssl=True)
dns = Test.MakeDNServer("dns")

request_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bad_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: badfoo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server_foo.addResponse("sessionlog.json", request_foo_header, response_header)
server_foo.addResponse("sessionlog.json", request_bad_foo_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")

ts.Disk.remap_config.AddLine(
    'map https://bar.com:{0}/ https://foo.com:{1}'.format(ts.Variables.ssl_port, server_foo.Variables.SSL_Port))
ts.Disk.remap_config.AddLine(
    'map https://foo.com:{0}/ https://bar.com:{1}'.format(ts.Variables.ssl_port, server_foo.Variables.SSL_Port))

ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        # set global policy
        'proxy.config.ssl.client.verify.server.policy': 'ENFORCED',
        'proxy.config.ssl.client.verify.server.properties': 'ALL',
        'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.client.CA.cert.filename': 'signer.pem',
        'proxy.config.url_remap.pristine_host_hdr': 0,
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.dns.resolv_conf': 'NULL'
    })

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})

# bar.com in.  foo.com out.  Should verify
tr = Test.AddTestRun("Enforced-Good-Test")
tr.Setup.Copy("ssl/signed-foo.key")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.Setup.Copy("ssl/signed-bar.pem")
tr.Processes.Default.Command = "curl -v --resolve 'bar.com:{0}:127.0.0.1' -k https://bar.com:{0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server_foo)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

# foo.com in.  bar.com out.  Should not verify
tr2 = Test.AddTestRun("Enforced-bad-test")
tr2.Processes.Default.Command = "curl -v -k --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}".format(ts.Variables.ssl_port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Curl attempt should not have succeeded")

# Over riding the built in ERROR check since we expect tr3 to fail
ts.Disk.diags_log.Content = Testers.ExcludesExpression("verification failed", "Make sure the signatures didn't fail")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    r"WARNING: SNI \(bar.com\) not in certificate", "Make sure bad_bar name checked failed.")
