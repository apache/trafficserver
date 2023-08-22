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
Test tls server certificate verification options. Exercise conf_remap
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True)
cafile = "{0}/signer.pem".format(Test.RunDirectory)

server_foo = Test.MakeOriginServer("server_foo",
                                   ssl=True,
                                   options={"--key": "{0}/signed-foo.key".format(Test.RunDirectory),
                                            "--cert": "{0}/signed-foo.pem".format(Test.RunDirectory),
                                            "--clientCA": cafile,
                                            "--clientverify": ""},
                                   clientcert="{0}/signed-bar.pem".format(Test.RunDirectory),
                                   clientkey="{0}/signed-bar.key".format(Test.RunDirectory))
server_bar = Test.MakeOriginServer("server_bar",
                                   ssl=True,
                                   options={"--key": "{0}/signed-foo.key".format(Test.RunDirectory),
                                            "--cert": "{0}/signed-foo.pem".format(Test.RunDirectory),
                                            "--clientCA": cafile,
                                            "--clientverify": ""},
                                   clientcert="{0}/signed-bar.pem".format(Test.RunDirectory),
                                   clientkey="{0}/signed-bar.key".format(Test.RunDirectory))

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

server_bar.Setup.Copy("ssl/signer.pem")
server_bar.Setup.Copy("ssl/signer2.pem")
server_foo.Setup.Copy("ssl/signer.pem")
server_foo.Setup.Copy("ssl/signer2.pem")

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
    'map http://foo.com/defaultbar https://bar.com:{0}'.format(server_bar.Variables.SSL_Port))
ts.Disk.remap_config.AddLine(
    'map http://foo.com/default https://foo.com:{0}'.format(server_foo.Variables.SSL_Port))
ts.Disk.remap_config.AddLine(
    'map http://foo.com/overridepolicy https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.policy=ENFORCED'.format(
        server_foo.Variables.SSL_Port))
ts.Disk.remap_config.AddLine(
    'map http://foo.com/overrideproperties https://bar.com:{0} @plugin=conf_remap.so @pparam=proxy.config.ssl.client.verify.server.properties=SIGNATURE'.format(
        server_foo.Variables.SSL_Port))

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# global config policy=permissive properties=all
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
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

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bar.com',
    '  client_cert: "{0}/signed-foo.pem"'.format(ts.Variables.SSLDir),
    '  client_key: "{0}/signed-foo.key"'.format(ts.Variables.SSLDir),
])

dns.addRecords(records={"foo.com.": ["127.0.0.1"]})
dns.addRecords(records={"bar.com.": ["127.0.0.1"]})
dns.addRecords(records={"random.com.": ["127.0.0.1"]})

# Should succeed with message
# exercise default settings
tr = Test.AddTestRun("default-permissive-success")
tr.Setup.Copy("ssl/signed-foo.key")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.Setup.Copy("ssl/signed-bar.pem")
tr.Processes.Default.Command = 'curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/defaultbar'.format(ts.Variables.port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(server_foo)
tr.Processes.Default.StartBefore(server_bar)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

# should fail.  Exercise the override
tr2 = Test.AddTestRun("policy-override-fail")
tr2.Processes.Default.Command = "curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/overridepolicy".format(ts.Variables.port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Curl attempt should fail")

# should succeed with an error message
tr2 = Test.AddTestRun("properties-override-permissive")
tr2.Processes.Default.Command = "curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/overrideproperties".format(ts.Variables.port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = ts
tr2.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")


# Over riding the built in ERROR check since we expect some cases to fail
ts.Disk.diags_log.Content = Testers.ContainsExpression(
    r"WARNING: SNI \(bar.com\) not in certificate. Action=Continue server=bar.com", "Warning for mismatch name not enforcing")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    r" WARNING: SNI \(bar.com\) not in certificate. Action=Terminate server=bar.com", "Warning for enforcing mismatch")
