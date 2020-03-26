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
Test for server_push_preload plugin
'''

Test.SkipUnless(
    Condition.PluginExists('server_push_preload.so'),
    Condition.HasProgram("nghttp",
                         "Nghttp need to be installed on system for this test to work"),
)
Test.testName = "server_push_preload"
Test.ContinueOnFail = True

# ----
# Setup Origin Server
# ----
microserver = Test.MakeOriginServer("microserver")

# index.html
microserver.addResponse("sessionfile.log",
                        {"headers": "GET /index.html HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "body": ""},
                        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nLink: </app/style.css>; rel=preload; as=style; nopush\r\nLink: </app/script.js>; rel=preload; as=script\r\n\r\n",
                         "body": "<html>\r\n<head>\r\n<link rel='stylesheet' type='text/css' href='/app/style.css' />\r\n<script src='/app/script.js'></script>\r\n</head>\r\n<body>\r\nServer Push Preload Test\r\n</body>\r\n</html>\r\n"})

# /app/style.css
microserver.addResponse("sessionfile.log",
                        {"headers": "GET /app/style.css HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "body": ""},
                        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                         "body": "body { font-weight: bold; }\r\n"})

# /app/script.js
microserver.addResponse("sessionfile.log",
                        {"headers": "GET /app/script.js HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "body": ""},
                        {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                         "body": "function do_nothing() { return; }\r\n"})

# ----
# Setup ATS
# ----
ts = Test.MakeATSProcess("ts", select_ports=True, enable_tls=True)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    'map / http://127.0.0.1:{0}/ @plugin=server_push_preload.so'.format(
        microserver.Variables.Port)
)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http2|server_push_preload',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http2.active_timeout_in': 3,
})

# ----
# Test Cases
# ----

# Test Case 0: Server Push by Link header
tr = Test.AddTestRun()
tr.Processes.Default.Command = "nghttp -vs --no-dep 'https://127.0.0.1:{0}/index.html'".format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(
    microserver, ready=When.PortOpen(microserver.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stdout = "gold/server_push_preload_0_stdout.gold"
tr.StillRunningAfter = microserver
