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
Test a basic remap of a http connection
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server")
server2 = Test.MakeOriginServer("server2", ssl=True)

# **testname is required**
testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
# desired response form the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)
server2.addResponse("sessionlog.json", request_header, response_header)

# add ssl materials like key, certificates for the server
ts.addDefaultSSLFiles()

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'lm|ssl',
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
    # enable ssl port
    'proxy.config.http.server_ports': '{0} {1}:proto=http2;http:ssl'.format(ts.Variables.port, ts.Variables.ssl_port),
    'proxy.config.ssl.client.verify.server.policy': 'PERMISSIVE',
})

ts.Disk.remap_config.AddLine(
    'map https://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    'map https://www.example.com:{1} http://127.0.0.1:{0}'.format(server.Variables.Port, ts.Variables.ssl_port)
)
ts.Disk.remap_config.AddLine(
    'map_with_recv_port https://www.example3.com:{1} http://127.0.0.1:{0}'.format(server.Variables.Port, ts.Variables.ssl_port)
)
ts.Disk.remap_config.AddLine(
    'map https://www.anotherexample.com https://127.0.0.1:{0}'.format(server2.Variables.SSL_Port, ts.Variables.ssl_port)
)


ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# call localhost straight
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} --verbose'.format(ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0

tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(server2)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/remap-hitATS-404.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts


# www.example.com host
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.example.com" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-https-200.gold"


# www.example.com:80 host
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.example.com:443" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-https-200.gold"

# www.example.com:8080 host
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.example.com:{0}" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-https-200.gold"

# www.example3.com (match on receive port)
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.example3.com" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-https-200_3.gold"

# no rule for this
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.test.com" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-hitATS-404.gold"

# bad port
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.example.com:1234" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-hitATS-404.gold"

# map www.anotherexample.com to https://<IP of microserver>.com
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --http1.1 -k https://127.0.0.1:{0} -H "Host: www.anotherexample.com" --verbose'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-https-200_2.gold"
tr.StillRunningAfter = server2
