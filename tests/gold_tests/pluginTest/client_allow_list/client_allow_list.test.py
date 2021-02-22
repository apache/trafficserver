'''
Basic test for client_allow_list plugin
#'''
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
Basic test for client_allow_list plugin
'''

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.addSSLfile("ssl/signer.pem")

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'client_allow_list',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.url_remap.pristine_host_hdr': 1,
    'proxy.config.ssl.client.certification_level': 2,
    'proxy.config.ssl.CA.cert.filename': '{0}/signer.pem'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.TLSv1_3': 0
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Just map everything through to origin.
ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}/'.format(server.Variables.Port)
)

ts.Disk.File(ts.Variables.CONFIGDIR + "/basic.yaml", id="basic_yaml", typename="ats:config")
ts.Disk.basic_yaml.AddLine('<none>|<other>: "*"')
ts.Disk.basic_yaml.AddLine("")
ts.Disk.basic_yaml.AddLine('aaa.com: ""')
ts.Disk.basic_yaml.AddLine("")
ts.Disk.basic_yaml.AddLine("bBb.com:")
ts.Disk.basic_yaml.AddLine('  - yada')
ts.Disk.basic_yaml.AddLine('  - signer.*.com')
ts.Disk.basic_yaml.AddLine('  - yadayada')
ts.Disk.basic_yaml.AddLine("")
ts.Disk.basic_yaml.AddLine("ccc.com:")
ts.Disk.basic_yaml.AddLine('  - yada')
ts.Disk.basic_yaml.AddLine('  - yadayada')
ts.Disk.basic_yaml.AddLine('  - foo.com')
ts.Disk.basic_yaml.AddLine("")
ts.Disk.basic_yaml.AddLine("ddd.com:")
ts.Disk.basic_yaml.AddLine('  - bar.com')
ts.Disk.basic_yaml.AddLine('  - yada')
ts.Disk.basic_yaml.AddLine('  - yadayada')

ts.Disk.plugin_config.AddLine('client_allow_list.so basic.yaml')

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.StartBefore(server)
tr.Setup.Copy("ssl/signed-foo.pem")
tr.Setup.Copy("ssl/signed-foo.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key " +
    "--resolve 'foo.com:{0}:127.0.0.1' https://foo.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun()
tr.Setup.Copy("ssl/signed-bar.pem")
tr.Setup.Copy("ssl/signed-bar.key")
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-bar.pem --key ./signed-bar.key " +
    "--resolve 'bar.com:{0}:127.0.0.1' https://bar.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "TLS handshake should succeed")

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key " +
    "--resolve 'BbB.com:{0}:127.0.0.1' https://BbB.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key " +
    "--resolve 'ccc.com:{0}:127.0.0.1' https://ccc.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "Check response")

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-bar.pem --key ./signed-bar.key " +
    "--resolve 'ddd.com:{0}:127.0.0.1' https://ddd.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "TLS handshake should succeed")

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-bar.pem --key ./signed-bar.key " +
    " https://127.0.0.1:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.All = Testers.ExcludesExpression("error", "TLS handshake should succeed")

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-foo.pem --key ./signed-foo.key " +
    "--resolve 'aaa.com:{0}:127.0.0.1' https://aaa.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 35

tr = Test.AddTestRun()
tr.StillRunningAfter = ts
tr.StillRunningAfter = server
tr.Processes.Default.Command = (
    "curl --tls-max 1.2 -k --cert ./signed-bar.pem --key ./signed-bar.key " +
    "--resolve 'aaa.com:{0}:127.0.0.1' https://aaa.com:{0}/xyz".format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 35

# This file was originally copied from tests/gold_tests/tls/tls_client_verify.test.py .
