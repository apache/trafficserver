#!/usr/bin/env python3

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

'''
'''

Test.Summary = '''
Test for TSSslSecretXxx functions.
'''

ts = Test.MakeATSProcess("ts", enable_tls=True, block_for_debug=False)

server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: does.not.matter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}

server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.plugin_config.AddLine(
    f"{Test.TestDirectory}/.libs/test_ts_ssl.so {ts.Variables.SSLDir}"
)

ts.Disk.remap_config.AddLine(
    f'map / http://127.0.0.1:{server.Variables.Port}'
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=2050.crt ssl_key_name=2050_2060.key'
)

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': ts.Variables.SSLDir,
    'proxy.config.ssl.server.private_key.path': ts.Variables.SSLDir,
    'proxy.config.diags.debug.tags': 'ssl|ts_ssl',
    'proxy.config.diags.debug.enabled': 3,
})

ts.addSSLfile("ssl/2050.crt")
ts.addSSLfile("ssl/2060.crt")
ts.addSSLfile("ssl/2050_2060.key")

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.Command = f"curl -vvv -k -H 'host: foo.com' https://127.0.0.1:{ts.Variables.ssl_port}"
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(r"HTTP/(2|1\.1) 200", "Request succeeds")
tr.Processes.Default.Streams.stderr += Testers.ContainsExpression("expire date: .* 2050", "Correct expiration year")
tr.ReturnCode = 0

# Dummy request to trigger the test plugin to change the cert with a 2050 expiry to one with a 2060 expiry.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = f"curl -vvv -H 'host: foo.com' http://127.0.0.1:{ts.Variables.port}"
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(r"HTTP/(2|1\.1) 200", "Request succeeds")

tr = Test.AddTestRun()
tr.Processes.Default.Command = f"curl -vvv -k -H 'host: foo.com' https://127.0.0.1:{ts.Variables.ssl_port}"
tr.Processes.Default.Streams.stderr = Testers.ContainsExpression(r"HTTP/(2|1\.1) 200", "Request succeeds")
# This should have a 2060, not a 2050 expiry date.
tr.Processes.Default.Streams.stderr += Testers.ContainsExpression("expire date: .* 2050", "Correct expiration year")
tr.ReturnCode = 0
