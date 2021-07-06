'''
Test per SNI server name selection of CA certs for validating cert sent by client.
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
Test per SNI server name selection of CA certs for validating cert sent by client.
'''

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

server = Test.MakeOriginServer("server")

request_header = {"headers": "GET /xyz HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "yadayadayada"}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Setup.Copy("ssl/bbb-ca.pem", ts.Variables.CONFIGDIR)
ts.Setup.Copy("ssl/bbb-signed.key", ts.Variables.SSLDir)
ts.Setup.Copy("ssl/bbb-signed.pem", ts.Variables.SSLDir)
ts.Setup.Copy("ssl/aaa-ca.pem", ts.Variables.SSLDir)
ts.Setup.Copy("ssl/ccc-ca.pem", ts.Variables.SSLDir)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'ssl',
    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.client.certification_level': 2,
    'proxy.config.ssl.CA.cert.filename': f'{ts.Variables.SSLDir}/aaa-ca.pem',
    'proxy.config.ssl.TLSv1_3': 0
})

ts.Disk.ssl_multicert_config.AddLine(
    'ssl_cert_name=bbb-signed.pem ssl_key_name=bbb-signed.key'
)
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Just map everything through to origin.  This test is concentrating on the user-agent side.
ts.Disk.remap_config.AddLine(
    f'map / http://127.0.0.1:{server.Variables.Port}/'
)

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: bbb.com',
    '  verify_client: STRICT',
    '  verify_client_ca_certs: bbb-ca.pem',
    '- fqdn: bbb-signed',
    '  verify_client: STRICT',
    '  verify_client_ca_certs: bbb-ca.pem',
    '- fqdn: ccc.com',
    '  verify_client: STRICT',
    '  verify_client_ca_certs:',
    f'    file: {ts.Variables.SSLDir}/ccc-ca.pem'
])

# Success test runs.

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl -v -k --tls-max 1.2  --cert {1}.pem --key {1}.key --resolve 'aaa.com:{0}:127.0.0.1'" +
    " https://aaa.com:{0}/xyz"
).format(ts.Variables.ssl_port, Test.TestDirectory + "/ssl/aaa-signed")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl -v -k --tls-max 1.2  --cert {1}.pem --key {1}.key --resolve 'bbb-signed:{0}:127.0.0.1'" +
    " https://bbb-signed:{0}/xyz"
).format(ts.Variables.ssl_port, Test.TestDirectory + "/ssl/bbb-signed")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl -v -k --tls-max 1.2  --cert {1}.pem --key {1}.key --resolve 'ccc.com:{0}:127.0.0.1'" +
    " https://ccc.com:{0}/xyz"
).format(ts.Variables.ssl_port, Test.TestDirectory + "/ssl/ccc-signed")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

# Failure test runs.

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl -v -k --tls-max 1.2  --cert {1}.pem --key {1}.key --resolve 'aaa.com:{0}:127.0.0.1'" +
    " https://aaa.com:{0}/xyz"
).format(ts.Variables.ssl_port, Test.TestDirectory + "/ssl/bbb-signed")
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl -v -k --tls-max 1.2  --cert {1}.pem --key {1}.key --resolve 'bbb.com:{0}:127.0.0.1'" +
    " https://bbb.com:{0}/xyz"
).format(ts.Variables.ssl_port, Test.TestDirectory + "/ssl/ccc-signed")
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl -v -k --tls-max 1.2  --cert {1}.pem --key {1}.key --resolve 'ccc.com:{0}:127.0.0.1'" +
    " https://ccc.com:{0}/xyz"
).format(ts.Variables.ssl_port, Test.TestDirectory + "/ssl/aaa-signed")
tr.Processes.Default.ReturnCode = 35
