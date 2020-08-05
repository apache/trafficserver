'''
Test the cert_update plugin.
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

import ports

Test.Summary = '''
Test cert_update plugin.
'''

Test.SkipUnless(
    Condition.HasProgram("openssl", "Openssl need to be installed on system for this test to work"),
    Condition.PluginExists('cert_update.so')
)

# Set up origin server
server = Test.MakeOriginServer("server")
request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# Set up ATS
ts = Test.MakeATSProcess("ts", command="traffic_manager", enable_tls=1)

# Set up ssl files
ts.addSSLfile("ssl/server1.pem")
ts.addSSLfile("ssl/server2.pem")
ts.addSSLfile("ssl/client1.pem")
ts.addSSLfile("ssl/client2.pem")

# reserve port, attach it to 'ts' so it is released later
ports.get_port(ts, 's_server_port')

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'cert_update',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.client.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.url_remap.pristine_host_hdr': 1
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server1.pem ssl_key_name=server1.pem'
)

ts.Disk.remap_config.AddLines([
    'map https://bar.com http://127.0.0.1:{0}'.format(server.Variables.Port),
    'map https://foo.com https://127.0.0.1:{0}'.format(ts.Variables.s_server_port)
])

ts.Disk.sni_yaml.AddLines([
    'sni:',
    '- fqdn: "*foo.com"',
    '  client_cert: "client1.pem"',
])

# Set up plugin
Test.PrepareInstalledPlugin('cert_update.so', ts)

# Server-Cert-Pre
# curl should see that Traffic Server presents bar.com cert from alice
tr = Test.AddTestRun("Server-Cert-Pre")
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = (
    'curl --verbose --insecure --ipv4 --resolve bar.com:{0}:127.0.0.1 https://bar.com:{0}'.format(ts.Variables.ssl_port)
)
tr.Processes.Default.Streams.stderr = "gold/server-cert-pre.gold"
tr.Processes.Default.ReturnCode = 0
tr.StillRunningAfter = server

# Server-Cert-Update
tr = Test.AddTestRun("Server-Cert-Update")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = (
    '{0}/traffic_ctl plugin msg cert_update.server {1}/server2.pem'.format(ts.Variables.BINDIR, ts.Variables.SSLDir)
)
ts.Streams.all = "gold/update.gold"
ts.StillRunningAfter = server

# Server-Cert-After
# after use traffic_ctl to update server cert, curl should see bar.com cert from bob
tr = Test.AddTestRun("Server-Cert-After")
tr.Processes.Default.Env = ts.Env
tr.Command = 'curl --verbose --insecure --ipv4 --resolve bar.com:{0}:127.0.0.1 https://bar.com:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.Streams.stderr = "gold/server-cert-after.gold"
tr.Processes.Default.ReturnCode = 0
ts.StillRunningAfter = server

# Client-Cert-Pre
# s_server should see client (Traffic Server) as alice.com
tr = Test.AddTestRun("Client-Cert-Pre")
s_server = tr.Processes.Process(
    "s_server",
    "openssl s_server -www -key {0}/server1.pem -cert {0}/server1.pem -accept {1} -Verify 1 -msg".format(
        ts.Variables.SSLDir,
        ts.Variables.s_server_port))
s_server.Ready = When.PortReady(ts.Variables.s_server_port)
tr.Command = 'curl --verbose --insecure --ipv4 --header "Host: foo.com" https://localhost:{}'.format(ts.Variables.ssl_port)
tr.Processes.Default.StartBefore(s_server)
s_server.Streams.all = "gold/client-cert-pre.gold"
tr.Processes.Default.ReturnCode = 0
ts.StillRunningAfter = server

# Client-Cert-Update
tr = Test.AddTestRun("Client-Cert-Update")
tr.Processes.Default.Env = ts.Env
tr.Processes.Default.Command = (
    'mv {0}/client2.pem {0}/client1.pem && {1}/traffic_ctl plugin msg cert_update.client {0}/client1.pem'.format(
        ts.Variables.SSLDir, ts.Variables.BINDIR)
)
ts.Streams.all = "gold/update.gold"
ts.StillRunningAfter = server

# Client-Cert-After
# after use traffic_ctl to update client cert, s_server should see client (Traffic Server) as bob.com
tr = Test.AddTestRun("Client-Cert-After")
s_server = tr.Processes.Process(
    "s_server",
    "openssl s_server -www -key {0}/server1.pem -cert {0}/server1.pem -accept {1} -Verify 1 -msg".format(
        ts.Variables.SSLDir,
        ts.Variables.s_server_port))
s_server.Ready = When.PortReady(ts.Variables.s_server_port)
tr.Processes.Default.Env = ts.Env
# Move client2.pem to replace client1.pem since cert path matters in client context mapping
tr.Command = 'curl --verbose --insecure --ipv4 --header "Host: foo.com" https://localhost:{0}'.format(ts.Variables.ssl_port)
tr.Processes.Default.StartBefore(s_server)
s_server.Streams.all = "gold/client-cert-after.gold"
tr.Processes.Default.ReturnCode = 0
ts.StillRunningAfter = server
