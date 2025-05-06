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
Test SNI configuration server_groups_list
'''
# The groups function was added in OpenSSL 1.1.1
Test.SkipUnless(Condition.HasOpenSSLVersion("1.1.1"))

# Define default ATS
ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server", ssl=True)

request_foo_header = {"headers": "GET / HTTP/1.1\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_foo_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "foo ok"}
server.addResponse("sessionlog.json", request_foo_header, response_foo_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

# Need no remap rules.  Everything should be processed by sni

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl_sni',
    })

ts.Disk.sni_yaml.AddLines(
    [
        'sni:',
        '- fqdn: aaa.com',
        '  server_groups_list: X25519MLKEM768',
        '  valid_tls_versions_in: [ TLSv1_3 ]',
        '  server_TLSv1_3_cipher_suites: TLS_AES_256_GCM_SHA384',
        '- fqdn: bbb.com',
        '  server_groups_list: x25519',
        '  valid_tls_versions_in: [ TLSv1_2 ]',
        '  server_cipher_suite: ECDHE-RSA-AES256-GCM-SHA384',
    ])

tr = Test.AddTestRun("Test 0: x25519")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.MakeCurlCommand(
    "-v --ciphers ECDHE-RSA-AES256-GCM-SHA384 --resolve 'bbb.com:{0}:127.0.0.1' -k  https://bbb.com:{0}".format(
        ts.Variables.ssl_port))
tr.ReturnCode = 0
tr.StillRunningAfter = ts
ts.Disk.traffic_out.Content += Testers.ContainsExpression(
    "Setting groups list from server_groups_list to x25519", "Should log setting the server groups")
tr.Processes.Default.Streams.stderr = Testers.IncludesExpression(
    f"SSL connection using TLSv1.2 / ECDHE-RSA-AES256-GCM-SHA384 / x25519", "Curl should log using x25519 in the SSL connection")

# Hybrid ECDH PQ key exchange TLS groups were added in OpenSSL 3.5
if Condition.HasOpenSSLVersion("3.5.0"):
    tr = Test.AddTestRun("Test 1: X25519MLKEM768")
    tr.MakeCurlCommand(
        "-v --tls13-ciphers TLS_AES_256_GCM_SHA384 --resolve 'aaa.com:{0}:127.0.0.1' -k  https://aaa.com:{0}".format(
            ts.Variables.ssl_port))
    tr.ReturnCode = 0
    tr.StillRunningAfter = ts
    ts.Disk.traffic_out.Content += Testers.ContainsExpression(
        "Setting groups list from server_groups_list to X25519MLKEM768", "Should log setting the server groups")
    tr.Processes.Default.Streams.stderr = Testers.IncludesExpression(
        f"SSL connection using TLSv1.3 / TLS_AES_256_GCM_SHA384 / X25519MLKEM768",
        f"Curl should log using X25519MLKEM768 in the SSL connection")
