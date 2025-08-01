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
Test TLS protocol offering  based on SNI
'''

# By default only offer TLSv1_2
# for special domain foo.com only offer TLSv1 and TLSv1_1

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
        'proxy.config.url_remap.pristine_host_hdr': 1,
        'proxy.config.ssl.server.version.min': 2,
        'proxy.config.ssl.server.version.max': 2,
        'proxy.config.ssl.TLSv1_2': 0,  # This setting should be ignored in favor of a version range setting
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'ssl',
    })

cipher_suite = 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2'
if Condition.HasOpenSSLVersion("3.0.0"):
    cipher_suite += ":@SECLEVEL=0"

# foo.com should only offer the older TLS protocols
# bar.com should terminate.
# empty SNI should tunnel to server_bar
ts.Disk.sni_yaml.AddLines(
    [
        'sni:',
        '- fqdn: foo.com',
        '  valid_tls_versions_in: [ TLSv1_2 ]',  # This setting should be ignored in favor of a version range setting
        '  valid_tls_version_min_in: TLSv1',
        '  valid_tls_version_max_in: TLSv1_1',
        f'  server_cipher_suite: {cipher_suite}',
    ])

# Target foo.com for TLSv1_2.  Should fail
tr = Test.AddTestRun("foo.com TLSv1_2")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
# Newer versions of OpenSSL further restrict the ciphers they accept. Setting
# the security level to 0 "retains compatibility with previous versions of
# OpenSSL." See:
# https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set_security_level.html
tr.MakeCurlCommand(
    "-v --ciphers DEFAULT@SECLEVEL=0 --tls-max 1.2 --tlsv1.2 --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(
        ts.Variables.ssl_port),
    ts=ts)
tr.ReturnCode = 35
tr.StillRunningAfter = ts

# Target foo.com for TLSv1.  Should succeed
tr = Test.AddTestRun("foo.com TLSv1")
tr.MakeCurlCommand(
    "-v --ciphers DEFAULT@SECLEVEL=0 --tls-max 1.0 --tlsv1 --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(
        ts.Variables.ssl_port),
    ts=ts)
tr.ReturnCode = 0
tr.StillRunningAfter = ts

# Target foo.com for TLSv1_1.  Should succeed
tr = Test.AddTestRun("foo.com TLSv1_1")
tr.MakeCurlCommand(
    "-v --ciphers DEFAULT@SECLEVEL=0 --tls-max 1.1 --tlsv1.1 --resolve 'foo.com:{0}:127.0.0.1' -k  https://foo.com:{0}".format(
        ts.Variables.ssl_port),
    ts=ts)
tr.ReturnCode = 0
tr.StillRunningAfter = ts

# Target bar.com for TLSv1.  Should fail
tr = Test.AddTestRun("bar.com TLSv1")
tr.MakeCurlCommand(
    "-v --ciphers DEFAULT@SECLEVEL=0 --tls-max 1.0 --tlsv1 --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(
        ts.Variables.ssl_port),
    ts=ts)
tr.ReturnCode = 35
tr.StillRunningAfter = ts

# Target bar.com for TLSv1_2.  Should succeed
tr = Test.AddTestRun("bar.com TLSv1_2")
tr.MakeCurlCommand(
    "-v --ciphers DEFAULT@SECLEVEL=0 --tls-max 1.2 --tlsv1.2 --resolve 'bar.com:{0}:127.0.0.1' -k  https://bar.com:{0}".format(
        ts.Variables.ssl_port),
    ts=ts)
tr.ReturnCode = 0
tr.StillRunningAfter = ts
