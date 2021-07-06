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
Test a basic remap of a websocket connections
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts", enable_tls=True)
server = Test.MakeOriginServer("server")

testName = "Test WebSocket Remaps"
request_header = {"headers": "GET /chat HTTP/1.1\r\nHost: www.example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
                  "body": None}
response_header = {
    "headers": "HTTP/1.1 101 OK\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n",
    "body": None}
server.addResponse("sessionlog.json", request_header, response_header)

ts.addDefaultSSLFiles()

ts.Disk.records_config.update({
    'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.SSLDir),
    'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.SSLDir),
})

ts.Disk.remap_config.AddLines([
    'map ws://www.example.com:{1} ws://127.0.0.1:{0}'.format(server.Variables.Port, ts.Variables.port),
    'map wss://www.example.com:{1} ws://127.0.0.1:{0}'.format(server.Variables.Port, ts.Variables.ssl_port)
])

ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)

# wss mapping
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=1)
tr.Processes.Default.Command = 'curl --max-time 2 -v -s -q -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" -H "Sec-WebSocket-Version: 13" --http1.1 --resolve www.example.com:{0}:127.0.0.1 -k https://www.example.com:{0}/chat'.format(
    ts.Variables.ssl_port)
tr.Processes.Default.ReturnCode = 28
tr.Processes.Default.Streams.stderr = "gold/remap-ws-upgrade.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# ws mapping
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --max-time 2 -v -s -q -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" -H "Sec-WebSocket-Version: 13" --http1.1 --resolve www.example.com:{0}:127.0.0.1 -k http://www.example.com:{0}/chat'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 28
tr.Processes.Default.Streams.stderr = "gold/remap-ws-upgrade.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts

# Missing required headers (should result in 400)
tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --max-time 2 -v -s -q -H "Connection: Upgrade" -H "Upgrade: websocket" --http1.1 --resolve www.example.com:{0}:127.0.0.1 -k http://www.example.com:{0}/chat'.format(
    ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "gold/remap-ws-upgrade-400.gold"
tr.StillRunningAfter = server
tr.StillRunningAfter = ts
