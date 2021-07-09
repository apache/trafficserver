'''
Test requiring certificate from user agent
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
Test various options for requiring certificate from client for mutual authentication TLS
'''

ts = Test.MakeATSProcess("ts", command="traffic_server", select_ports=True, enable_tls=True)
cafile = "{0}/signer.pem".format(Test.RunDirectory)
cafile2 = "{0}/signer2.pem".format(Test.RunDirectory)
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server2")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.client.certification_level': 2,
    'proxy.config.ssl.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.TLSv1_3': 0
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Just map everything through to origin.  This test is concentrating on the user-agent side
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}/'.format(server.Variables.Port)
)

# Scenario 1:  Default no client cert required.  cert required for bar.com
ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bob.bar.com',
    '  verify_client: NONE',
    '- fqdn: "bob.com"',
    '  verify_client: STRICT',
    '- fqdn: bob.*.com',
    '  verify_client: NONE',
    '- fqdn: "*bar.com"',
    '  verify_client: STRICT',
])

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: testformat
      format: '%<pssc> %<cquc> %<pscert> %<cscert>'
  logs:
    - mode: ascii
      format: testformat
      filename: squid
'''.split("\n")
)

# to foo.com w/o client cert.  Should fail
tr = Test.AddTestRun("Connect to foo.com without cert")
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/case1".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun("Connect to foo.com with bad cert")
tr.Setup.Copy("ssl/server.pem")
tr.Setup.Copy("ssl/server.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./server.pem --key ./server.key --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/case2".format(
    ts.Variables.ssl_port)
# Should fail with badly signed certs
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun("Connect to foo.com with cert")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key --resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/case3".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun("Connect to bob.bar.com without cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --resolve 'bob.bar.com:{0}:127.0.0.1' https://bob.bar.com:{0}/case4".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("alert", "TLS handshake should succeed")

tr = Test.AddTestRun("Connect to bob.bar.com with cert")
tr.Setup.Copy("ssl/signed-bob-bar.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-bob-bar.pem --key ./signed-bar.key --resolve 'bob.bar.com:{0}:127.0.0.1' https://bob.bar.com:{0}/case5".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun("Connect to bob.bar.com with bad cert")
tr.Setup.Copy("ssl/server.pem")
tr.Setup.Copy("ssl/server.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./server.pem --key ./server.key --resolve 'bob.bar.com:{0}:127.0.0.1' https://bob.bar.com:{0}/case6".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun("Connect to bob.foo.com without cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --resolve 'bob.foo.com:{0}:127.0.0.1' https://bob.foo.com:{0}/case7".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("alert", "TLS handshake should succeed")

tr = Test.AddTestRun("Connect to bob.foo.com with cert")
tr.Setup.Copy("ssl/signed-bob-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-bob-foo.pem --key ./signed-foo.key --resolve 'bob.foo.com:{0}:127.0.0.1' https://bob.foo.com:{0}/case8".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun("Connect to bob.foo.com with bad cert")
tr.Setup.Copy("ssl/server.pem")
tr.Setup.Copy("ssl/server.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./server.pem --key ./server.key --resolve 'bob.foo.com:{0}:127.0.0.1' https://bob.foo.com:{0}/case9".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun("Connect to bar.com without cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/case10".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun("Connect to bar.com with cert")
tr.Setup.Copy("ssl/signed-bar.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-bar.pem --key ./signed-bar.key --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/case11".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "TLS handshake should succeed")

tr = Test.AddTestRun("Connect to bar.com with bad cert")
tr.Setup.Copy("ssl/server.pem")
tr.Setup.Copy("ssl/server.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./server.pem --key ./server.key --resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/case12".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 35


# Test that the fqdn's match completely.  bob.com should require client certificate. bob.com.com should not
tr = Test.AddTestRun("Connect to bob.com without cert, should fail")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --resolve 'bob.com:{0}:127.0.0.1' https://bob.com:{0}/case13".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun("Connect to bob.com.com without cert, should succeed")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --resolve 'bob.com.com:{0}:127.0.0.1' https://bob.com.com:{0}/case14".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun("Wait for the access log to write out")
tr.Processes.Default.StartBefore(server2, ready=When.FileExists(ts.Disk.squid_log))
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.StillRunningAfter = server2
tr.Processes.Default.Command = 'echo "Log file exists"'
tr.Processes.Default.ReturnCode = 0

ts.Disk.squid_log.Content = "gold/clientcert-accesslog.gold"
