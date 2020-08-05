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


# Someday should add client cert to origin to exercise the
# engine interface on the other side

Test.Summary = '''
Test tls via the async interface with the sample async_engine
'''

Test.SkipUnless(Condition.HasOpenSSLVersion('1.1.1'))

# Define default ATS
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)
server = Test.MakeOriginServer("server")

# Compile with tsxs.  That should bring in the consisten versions of openssl
ts.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, '../../contrib/openssl', 'async_engine.c'), Test.RunDirectory)
ts.Setup.RunCommand("tsxs -o async_engine.so async_engine.c")

# Add info the origin server responses
server.addResponse("sessionlog.json",
                   {"headers": "GET / HTTP/1.1\r\nuuid: basic\r\n\r\n",
                    "timestamp": "1469733493.993",
                    "body": ""},
                   {"headers": "HTTP/1.1 200 OK\r\nServer: microserver\r\nConnection: close\r\nCache-Control: max-age=3600\r\nContent-Length: 2\r\n\r\n",
                       "timestamp": "1469733493.993",
                       "body": "ok"})

# add ssl materials like key, certificates for the server
ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}'.format(server.Variables.Port)
)

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.exec_thread.autoconfig.scale': 1.0,
    'proxy.config.ssl.server.cipher_suite': 'ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:ECDHE-RSA-AES256-SHA384:AES128-GCM-SHA256:AES256-GCM-SHA384:ECDHE-RSA-RC4-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:RC4-SHA:RC4-MD5:AES128-SHA:AES256-SHA:DES-CBC3-SHA!SRP:!DSS:!PSK:!aNULL:!eNULL:!SSLv2',
    'proxy.config.ssl.engine.conf_file': '{0}/ts/config/load_engine.cnf'.format(Test.RunDirectory),
    'proxy.config.ssl.async.handshake.enabled': 1,
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'ssl'
})

ts.Disk.MakeConfigFile('load_engine.cnf').AddLines([
    'openssl_conf = openssl_init',
    '',
    '[openssl_init]',
    '',
    'engines = engine_section',
    '',
    '[engine_section]',
    '',
    'async = async_section',
    '',
    '[async_section]',
    '',
    'dynamic_path = {0}/async_engine.so'.format(Test.RunDirectory),
    '',
    'engine_id = async-test',
    '',
    'default_algorithms = RSA',
    '',
    'init = 1'])

# Make a basic request.  Hopefully it goes through
tr = Test.AddTestRun("Run-Test")
tr.Processes.Default.Command = "curl -k -v -H uuid:basic -H host:example.com  https://127.0.0.1:{0}/".format(ts.Variables.ssl_port)
tr.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.ssl_port))
tr.Processes.Default.Streams.All = Testers.ContainsExpression(r"HTTP/(2|1\.1) 200", "Request succeeds")
tr.StillRunningAfter = server

ts.Streams.All += Testers.ContainsExpression("Send signal to ", "The Async engine triggers")
