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
import ports

Test.Summary = '''
Forwarding a non-HTTP protocol out of TLS
'''

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

# reserve a port of the s_client that will be released with 'ts'
ports.get_port(ts, 's_client_port')

nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

# Need no remap rules.  Everything should be processed by sni

# Make sure the TS server certs are different from the origin certs
ts.Disk.ssl_multicert_config.AddLine('dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key')

# Case 1, global config policy=permissive properties=signature
#         override for foo.com policy=enforced properties=all
ts.Disk.records_config.update(
    {
        'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
        'proxy.config.http.connect_ports': '{0} {1}'.format(ts.Variables.ssl_port, ts.Variables.s_client_port),
        'proxy.config.exec_thread.autoconfig.scale': 1.0,
        'proxy.config.url_remap.pristine_host_hdr': 1,
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL'
    })

# foo.com should not terminate.  Just tunnel to server_foo
# bar.com should terminate.  Forward its tcp stream to server_bar
ts.Disk.sni_yaml.AddLines([
    "sni:",
    "- fqdn: bar.com",
    "  forward_route: localhost:{0}".format(ts.Variables.s_client_port),
])

tr = Test.AddTestRun("forward-non-http")
tr.Setup.Copy("test-nc-s_client.sh")
cmd_args = ["sh", "test-nc-s_client.sh", str(ts.Variables.s_client_port), str(ts.Variables.ssl_port)]
if Condition.HasOpenSSLVersion("3.0.0"):
    cmd_args += ["-ignore_unexpected_eof"]
tr.Processes.Default.Command = " ".join(cmd_args)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(nameserver)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts
testout_path = os.path.join(Test.RunDirectory, "test.out")
tr.Disk.File(testout_path, id="testout")
tr.Processes.Default.Streams.All += Testers.IncludesExpression("This is a reply", "s_client should get response")
