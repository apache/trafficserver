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
Test tls server  certificate verification options. Exercise conf_remap for ca bundle
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True)
server1 = Test.MakeOriginServer("server1",
                                ssl=True,
                                options={"--key": "{0}/signed-foo.key".format(Test.RunDirectory),
                                         "--cert": "{0}/signed-foo.pem".format(Test.RunDirectory)})
server2 = Test.MakeOriginServer("server2",
                                ssl=True,
                                options={"--key": "{0}/signed-foo.key".format(Test.RunDirectory),
                                         "--cert": "{0}/signed2-foo.pem".format(Test.RunDirectory)})

request_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bad_foo_header = {"headers": "GET / HTTP/1.1\r\nHost: bad_foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
request_bad_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bad_bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server1.addResponse("sessionlog.json", request_foo_header, response_header)
server1.addResponse("sessionlog.json", request_bad_foo_header, response_header)
server2.addResponse("sessionlog.json", request_bar_header, response_header)
server2.addResponse("sessionlog.json", request_bad_bar_header, response_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed2-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")
ts.addSSLfile("ssl/signer.key")
ts.addSSLfile("ssl/signer2.pem")
ts.addSSLfile("ssl/signer2.key")

ts.Disk.remap_config.AddLine(
    'map /case1 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.CA.cert.filename={1}/{2}'.format(
        server1.Variables.SSL_Port, ts.Variables.SSLDir, "signer.pem")
)
ts.Disk.remap_config.AddLine(
    'map /badcase1 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.CA.cert.filename={1}/{2}'.format(
        server1.Variables.SSL_Port, ts.Variables.SSLDir, "signer2.pem")
)
ts.Disk.remap_config.AddLine(
    'map /case2 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.CA.cert.filename={1}/{2}'.format(
        server2.Variables.SSL_Port, ts.Variables.SSLDir, "signer2.pem")
)
ts.Disk.remap_config.AddLine(
    'map /badcase2 https://127.0.0.1:{0}/ @plugin=conf_remap.so @pparam=proxy.config.ssl.client.CA.cert.filename={1}/{2}'.format(
        server2.Variables.SSL_Port, ts.Variables.SSLDir, "signer.pem")
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    # set global policy
    'proxy.config.ssl.client.verify.server.policy': 'ENFORCED',
    'proxy.config.ssl.client.verify.server.properties': 'SIGNATURE',
    'proxy.config.ssl.client.CA.cert.path': '/tmp',
    'proxy.config.ssl.client.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.url_remap.pristine_host_hdr': 1
})

# Should succeed
tr = Test.AddTestRun("Use correct ca bundle for server 1")
tr.Processes.Default.Command = 'curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/case1'.format(ts.Variables.port)
tr.ReturnCode = 0
tr.Setup.Copy("ssl/signed-foo.key")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed2-foo.pem")
tr.Processes.Default.StartBefore(server1)
tr.Processes.Default.StartBefore(server2)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = server1
tr.StillRunningAfter = ts
# Should succeed.  No message
tr.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr2 = Test.AddTestRun("Use incorrect ca  bundle for server 1")
tr2.Processes.Default.Command = "curl -k -H \"host: bar.com\"  http://127.0.0.1:{0}/badcase1".format(ts.Variables.port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server1
tr2.StillRunningAfter = ts
# Should succeed, but will be message in log about name mismatch
tr2.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Curl attempt should have succeeded")

tr2 = Test.AddTestRun("Use currect ca bundle for server 2")
tr2.Processes.Default.Command = "curl -k -H \"host: random.com\"  http://127.0.0.1:{0}/case2".format(ts.Variables.port)
tr2.ReturnCode = 0
tr2.StillRunningAfter = server2
tr2.StillRunningAfter = ts
# Should succeed, but will be message in log about signature
tr2.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")

tr3 = Test.AddTestRun("User incorrect ca bundle for server 2")
tr3.Processes.Default.Command = "curl -k -H \"host: foo.com\"  http://127.0.0.1:{0}/badcase2".format(ts.Variables.port)
tr3.ReturnCode = 0
tr3.StillRunningAfter = server2
tr3.StillRunningAfter = ts
# Should succeed.  No error messages
tr3.Processes.Default.Streams.stdout = Testers.ContainsExpression("Could Not Connect", "Curl attempt should have succeeded")
