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
Test TS API Hooks.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_TLS_ALPN'),
    Condition.HasCurlFeature('http2'),
)
Test.ContinueOnFail = True

# test_hooks.so will output test logging to this file.
Test.Env["OUTPUT_FILE"] = Test.RunDirectory + "/log.txt"

server = Test.MakeOriginServer("server")

request_header = {
    "headers": "GET /argh HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": "" }
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "" }
server.addResponse("sessionlog.json", request_header, response_header)

ts = Test.MakeATSProcess("ts", select_ports=False)

ts.addSSLfile("ssl/server.pem")
ts.addSSLfile("ssl/server.key")

ts.Variables.ssl_port = 4443

ts.Disk.records_config.update({
    'proxy.config.http.cache.http': 0,  # Make sure each request is forwarded to the origin server.
    'proxy.config.proxy_name': 'Poxy_Proxy',  # This will be the server name.
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.http.server_ports': (
        'ipv4:{0} ipv4:{1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port)),
    'proxy.config.url_remap.remap_required': 0,
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http|test_hooks',
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

Test.PreparePlugin(Test.Variables.AtsTestToolsDir + '/plugins/test_hooks.cc', ts)

ts.Disk.remap_config.AddLine(
    "map http://one http://127.0.0.1:{0}".format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    "map https://one http://127.0.0.1:{0}".format(server.Variables.Port)
)

tr = Test.AddTestRun()
# Wait for the micro server
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
#
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --header "Host: one" http://localhost:{0}/argh'.format(ts.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
# A small delay so the test_hooks test plugin can assume there is only one HTTP transaction in progress at a time.
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http2 --insecure --header "Host: one" https://localhost:{0}/argh'.format(ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "echo check log"
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File("log.txt")
f.Content = "log.gold"
