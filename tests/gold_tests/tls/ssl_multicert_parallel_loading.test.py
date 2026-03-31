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
Test parallel loading of ssl_multicert.config with multiple distinct SNI certs.
Verifies that the correct certificate is served for each domain when loaded
concurrently, and that errors in one cert line don't break others.
'''

##########################################################################
# Test 1: Multiple distinct SNI certs loaded in parallel, verify each
# domain gets the correct certificate.

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: foo.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.private_key.path': f'{ts.Variables.SSLDir}',
        'proxy.config.ssl.server.multicert.concurrency': 4,
        'proxy.config.ssl.server.multicert.exit_on_load_fail': 0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_load',
    })

# Copy all the distinct certs into the test's SSL directory
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/signed-san.pem")
ts.addSSLfile("ssl/signed-san.key")
ts.addSSLfile("ssl/signed-wild.pem")
ts.addSSLfile("ssl/signed-wild.key")
ts.addSSLfile("ssl/combo.pem")

ts.Disk.remap_config.AddLine(f'map / http://127.0.0.1:{server.Variables.Port}')

# 6 cert lines: 4 distinct domains + 1 default + 1 invalid.
# With concurrency=4 and firstLoad, this exercises multiple threads.
ts.Disk.ssl_multicert_config.AddLines(
    [
        'ssl_cert_name=signed-foo.pem ssl_key_name=signed-foo.key',
        'ssl_cert_name=signed-bar.pem ssl_key_name=signed-bar.key',
        'ssl_cert_name=signed-san.pem ssl_key_name=signed-san.key',
        'ssl_cert_name=signed-wild.pem ssl_key_name=signed-wild.key',
        'dest_ip=* ssl_cert_name=combo.pem',
        'ssl_cert_name=nonexistent.pem ssl_key_name=nonexistent.key',
    ])

# Test 1a: Verify foo.com gets the foo cert
tr1 = Test.AddTestRun("Verify foo.com gets correct cert")
tr1.Processes.Default.StartBefore(ts)
tr1.Processes.Default.StartBefore(server)
tr1.StillRunningAfter = ts
tr1.StillRunningAfter = server
tr1.MakeCurlCommand(f"-q -s -v -k --resolve 'foo.com:{ts.Variables.ssl_port}:127.0.0.1' https://foo.com:{ts.Variables.ssl_port}")
tr1.Processes.Default.ReturnCode = 0
tr1.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check connection")
tr1.Processes.Default.Streams.stderr = Testers.IncludesExpression("CN=foo.com", "Verify foo.com cert served")

# Test 1b: Verify bar.com gets the bar cert
tr2 = Test.AddTestRun("Verify bar.com gets correct cert")
tr2.StillRunningAfter = ts
tr2.StillRunningAfter = server
tr2.MakeCurlCommand(f"-q -s -v -k --resolve 'bar.com:{ts.Variables.ssl_port}:127.0.0.1' https://bar.com:{ts.Variables.ssl_port}")
tr2.Processes.Default.ReturnCode = 0
tr2.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check connection")
tr2.Processes.Default.Streams.stderr = Testers.IncludesExpression("CN=bar.com", "Verify bar.com cert served")

# Test 1c: Verify group.com (from signed-san.pem) gets the san cert
tr3 = Test.AddTestRun("Verify group.com gets correct cert")
tr3.StillRunningAfter = ts
tr3.StillRunningAfter = server
tr3.MakeCurlCommand(
    f"-q -s -v -k --resolve 'group.com:{ts.Variables.ssl_port}:127.0.0.1' https://group.com:{ts.Variables.ssl_port}")
tr3.Processes.Default.ReturnCode = 0
tr3.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check connection")
tr3.Processes.Default.Streams.stderr = Testers.IncludesExpression("CN=group.com", "Verify group.com cert served")

# Test 1d: Verify unknown domain gets the default cert (combo.pem = random.server.com)
tr4 = Test.AddTestRun("Verify unknown domain gets default cert")
tr4.StillRunningAfter = ts
tr4.StillRunningAfter = server
tr4.MakeCurlCommand(
    f"-q -s -v -k --resolve 'unknown.example.com:{ts.Variables.ssl_port}:127.0.0.1' https://unknown.example.com:{ts.Variables.ssl_port}"
)
tr4.Processes.Default.ReturnCode = 0
tr4.Processes.Default.Streams.stdout = Testers.ExcludesExpression("Could Not Connect", "Check connection")
tr4.Processes.Default.Streams.stderr = Testers.IncludesExpression("CN=random.server.com", "Verify default cert served")

# Verify the invalid cert line produced an error but didn't break the others
ts.Disk.diags_log.Content = Testers.ContainsExpression('failed to load certificate', 'invalid cert line should produce error')
