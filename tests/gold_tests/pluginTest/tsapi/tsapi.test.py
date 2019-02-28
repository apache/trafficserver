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
Test TS API.
'''

Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_TLS_ALPN'),
    Condition.HasCurlFeature('http2'),
)
Test.ContinueOnFail = True

# test_tsapi.so will output test logging to this file.
Test.Env["OUTPUT_FILE"] = Test.RunDirectory + "/log.txt"

server = Test.MakeOriginServer("server")

request_header = {
    "headers": "GET / HTTP/1.1\r\nHost: doesnotmatter\r\n\r\n", "timestamp": "1469733493.993", "body": "" }
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": "112233" }
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
    'proxy.config.diags.debug.tags': 'http|test_tsapi',
})

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

ts.Disk.remap_config.AddLine(
    "map http://myhost.test:{0}  http://127.0.0.1:{0}".format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    "map https://myhost.test:{0}  http://127.0.0.1:{0}".format(server.Variables.Port)
)

Test.PreparePlugin(Test.Variables.AtsTestToolsDir + '/plugins/test_tsapi.cc', ts)

tr = Test.AddTestRun()
# Probe server port to check if ready.
tr.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
# Probe TS cleartext port to check if ready.
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=When.PortOpen(ts.Variables.port))
#
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --header "Host: myhost.test:{0}" http://localhost:{1}/'.format(server.Variables.Port, ts.Variables.port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    'curl --verbose --ipv4 --http2 --insecure --header ' +
    '"Host: myhost.test:{0}" https://localhost:{1}/'.format(server.Variables.Port, ts.Variables.ssl_port)
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
# Change server port number (which can vary) to a fixed string for compare to gold file.
tr.Processes.Default.Command = "sed 's/:{0}/:SERVER_PORT/' < {1}/log.txt > {1}/log2.txt".format(
    server.Variables.Port, Test.RunDirectory)
tr.Processes.Default.ReturnCode = 0
f = tr.Disk.File("log2.txt")
f.Content = "log.gold"
