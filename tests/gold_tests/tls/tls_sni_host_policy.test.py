'''
Test exercising host and SNI mismatch controls
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
Test exercising host and SNI mismatch controls
'''

ts = Test.MakeATSProcess("ts", enable_tls=True)
cafile = "{0}/signer.pem".format(Test.RunDirectory)
cafile2 = "{0}/signer2.pem".format(Test.RunDirectory)
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET /case1 HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
request_header = {"headers": "GET /  HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.http.host_sni_policy': 2,
    'proxy.config.ssl.TLSv1_3.enabled': 0,
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl',
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Just map everything through to origin.  This test is concentrating on the user-agent side
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}/'.format(server.Variables.Port)
)

# Scenario 1:  Default no client cert required.  cert required for bar.com.
# Make boblite and bob mixed case to verify that we can match hostnames case
# insensitively.
ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: boBliTe',
    '  verify_client: STRICT',
    '  host_sni_policy: PERMISSIVE',
    '- fqdn: bOb',
    '  verify_client: STRICT',
])

# case 1
# sni=Bob and host=dave.  Do not provide client cert.  This should match fqdn bOb which has
# verify_client: STRICT and thus should fail.
tr = Test.AddTestRun("Connect to bob without cert")
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k -H 'host:dave' --resolve 'Bob:{0}:127.0.0.1' https://Bob:{0}/case1".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 35

# case 2
# sni=Bob and host=dave.  Do provide client cert.  This should match fqdn bOb which has
# verify_client: STRICT, but since the cert is good it should succeed.
tr = Test.AddTestRun("Connect to bob with good cert")
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key -H 'host:dave' --resolve 'Bob:{0}:127.0.0.1' https://Bob:{0}/case1".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

# case 3
# sni=dave and host=bob.  Do not provide client cert.  This should not need a cert, but should still
# fail due to sni-host mismatch.
tr = Test.AddTestRun("Connect to dave without cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k -H 'host:Bob' --resolve 'dave:{0}:127.0.0.1' https://dave:{0}/case1".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression("Access Denied", "Check response")

# case 4
# sni=dave and host=bob.  Do provide client cert.  Again, this should not need a cert, but provide
# one anyway and verify it fails due to sni-host mismatch.
tr = Test.AddTestRun("Connect to dave with cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key -H 'host:bob' --resolve 'dave:{0}:127.0.0.1' https://dave:{0}/case1".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ContainsExpression("Access Denied", "Check response")

# case 5
# sni=Bob and host=boB.  Do provide client cert.  Should succeed because the hosts match and the cert is provided.
tr = Test.AddTestRun("Connect to bob with cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key -H 'host:boB' --resolve 'Bob:{0}:127.0.0.1' https://bob:{0}/case1".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Access Denied", "Check response")

# case 6
# sni=ellen and host=Boblite.  Do not provide client cert.  Should warn due to sni-host mismatch,
# but should note get an Access Denied because boblite has host_sni_policy: PERMISSIVE configured.
tr = Test.AddTestRun("Connect to ellen without cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k -H 'host:Boblite' --resolve 'ellen:{0}:127.0.0.1' https://ellen:{0}/warnonly".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Access Denied", "Check response")

# case 7
# sni=ellen and host=Boblite.  Do provide client cert.  This should behave the same as above because
# providing a client cert does not mean SNI and hostname will not be compared.
tr = Test.AddTestRun("Connect to ellen with cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key -H 'host:Boblite' --resolve 'ellen:{0}:127.0.0.1' https://ellen:{0}/warnonly".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Access Denied", "Check response")

# case 8
# sni=ellen and host=fran.  Do not provide client cert.  No warning since neither name is mentioned in sni.yaml
tr = Test.AddTestRun("Connect to ellen without cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k -H 'host:fran' --resolve 'ellen:{0}:127.0.0.1' https://ellen:{0}/warnonly".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Access Denied", "Check response")

# case 9
# sni=ellen and host=fran.  Do provide client cert.  No warning since neither name is mentioned in sni.yaml
tr = Test.AddTestRun("Connect to ellen with cert")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = "curl -v --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key -H 'host:fran' --resolve 'ellen:{0}:127.0.0.1' https://ellen:{0}/warnonly".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("Access Denied", "Check response")

# Wait for the error.log to appaer.
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' +
    os.path.join(ts.Variables.LOGDIR, 'error.log')
)

ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "WARNING: SNI/hostname mismatch sni=dave host=bob action=terminate", "Should have warning on mismatch")
ts.Disk.diags_log.Content += Testers.ContainsExpression(
    "WARNING: SNI/hostname mismatch sni=ellen host=Boblite action=continue", "Should have warning on mismatch")
ts.Disk.diags_log.Content += Testers.ExcludesExpression("WARNING: SNI/hostname mismatch sni=ellen host=fran",
                                                        "Should not have warning on mismatch with non-policy host")

test_run.Processes.Default.ReturnCode = 0
ts.Disk.error_log.Content += Testers.ContainsExpression(
    "SNI/hostname mismatch: connecting to .* for host='bob' sni='dave', returning a 403",
    "error.log should contain information about the 403 response.")
