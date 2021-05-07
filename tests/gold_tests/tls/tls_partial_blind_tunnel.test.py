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

Test.Summary = 'Testing partial_blind_tunnel'

ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server_bar = Test.MakeOriginServer("server_bar", ssl=True)
nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

request_bar_header = {"headers": "GET / HTTP/1.1\r\nHost: bar.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_bar_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "ok bar"}
server_bar.addResponse("sessionlog_bar.json", request_bar_header, response_bar_header)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/signed-foo.pem")
ts.addSSLfile("ssl/signed-foo.key")
ts.addSSLfile("ssl/signed-bar.pem")
ts.addSSLfile("ssl/signed-bar.key")
ts.addSSLfile("ssl/signer.pem")

# Need no remap rules. Everything should be processed by sni

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=signed-foo.pem ssl_key_name=signed-foo.key'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.connect_ports': '{0} {1}'.format(ts.Variables.ssl_port, server_bar.Variables.SSL_Port),
    'proxy.config.ssl.client.CA.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.CA.cert.filename': 'signer.pem',
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
    'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
    'proxy.config.dns.resolv_conf': 'NULL'
})

# foo.com should terminate. and reconnect via TLS upstream to bar.com
ts.Disk.sni_yaml.AddLines([
    "sni:",
    "- fqdn: 'foo.com'",
    "  partial_blind_route: 'localhost:{0}'".format(server_bar.Variables.SSL_Port),
])

tr = Test.AddTestRun("Partial Blind Route")
tr.Processes.Default.Command = "curl --http1.1 -v --resolve 'foo.com:{0}:127.0.0.1' -k https://foo.com:{0}".format(
    ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server_bar)
tr.Processes.Default.StartBefore(nameserver)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts

tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Could Not Connect", "Curl attempt should have succeeded")
tr.Processes.Default.Streams.All += Testers.ExcludesExpression("Not Found on Accelerato",
                                                               "Should not try to remap on Traffic Server")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("HTTP/1.1 200 OK", "Should get a successful response")
tr.Processes.Default.Streams.All += Testers.ContainsExpression("ok bar", "Body is expected")
