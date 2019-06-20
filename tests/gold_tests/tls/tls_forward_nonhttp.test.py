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

import os
Test.Summary = '''
Forwarding a non-HTTP protocol out of TLS
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

# Need no remap rules.  Everything should be proccessed by sni

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.http.connect_ports': '{0} 4444'.format(ts.Variables.ssl_port),
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.url_remap.pristine_host_hdr': 1
})

# foo.com should not terminate.  Just tunnel to server_foo
# bar.com should terminate.  Forward its tcp stream to server_bar
ts.Disk.sni_yaml.AddLines([
  "sni:",
  "- fqdn: bar.com",
  "  forward_route: localhost:4444"
  ])

tr = Test.AddTestRun("forward-non-http")
tr.Setup.Copy("test-nc-s_client.sh")
tr.Processes.Default.Command = "sh test-nc-s_client.sh 4444 {0}".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
testout_path = os.path.join(Test.RunDirectory, "test.out")
tr.Disk.File(testout_path, id = "testout")
tr.Processes.Default.Streams.All += Testers.IncludesExpression("This is a reply", "s_client should get response")
