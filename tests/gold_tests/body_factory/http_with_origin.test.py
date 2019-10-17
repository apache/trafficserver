'''
Tests that HEAD requests return proper responses
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
Tests that HEAD requests return proper responses
'''

ts = Test.MakeATSProcess("ts")

HOST = 'www.example.test'

server = Test.MakeOriginServer("server")

ts.Disk.remap_config.AddLine(
    'map http://{0} http://127.0.0.1:{1}'.format(HOST, server.Variables.Port)
)

server.addResponse("sessionfile.log", {
    "headers": "HEAD /head200 HTTP/1.1\r\nHost: {0}\r\n\r\n".format(HOST),
    "timestamp": "1469733493.993",
    "body": ""
}, {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "This body should not be returned for a HEAD request."
})

server.addResponse("sessionfile.log", {
    "headers": "GET /get200 HTTP/1.1\r\nHost: {0}\r\n\r\n".format(HOST),
    "timestamp": "1469733493.993",
    "body": ""
}, {
    "headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": "This body should be returned for a GET request."
})

server.addResponse("sessionfile.log", {
    "headers": "GET /get304 HTTP/1.1\r\nHost: {0}\r\n\r\n".format(HOST),
    "timestamp": "1469733493.993",
    "body": ""
}, {
    "headers": "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n",
    "timestamp": "1469733493.993",
    "body": ""
})


Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
Test.Setup.Copy('data')

trhead200 = Test.AddTestRun("Test domain {0}".format(HOST))
trhead200.Processes.Default.StartBefore(Test.Processes.ts)
trhead200.Processes.Default.StartBefore(server, ready=When.PortOpen(server.Variables.Port))
trhead200.StillRunningAfter = ts
trhead200.StillRunningAfter = server

trhead200.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_head_200.txt'.format(HOST))
trhead200.Processes.Default.TimeOut = 5  # seconds
trhead200.Processes.Default.ReturnCode = 0
trhead200.Processes.Default.Streams.stdout = "gold/http-head-200.gold"


trget200 = Test.AddTestRun("Test domain {0}".format(HOST))
trget200.StillRunningBefore = ts
trget200.StillRunningBefore = server
trget200.StillRunningAfter = ts
trget200.StillRunningAfter = server

trget200.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_get_200.txt'.format(HOST))
trget200.Processes.Default.TimeOut = 5  # seconds
trget200.Processes.Default.ReturnCode = 0
trget200.Processes.Default.Streams.stdout = "gold/http-get-200.gold"


trget304 = Test.AddTestRun("Test domain {0}".format(HOST))
trget304.StillRunningBefore = ts
trget304.StillRunningBefore = server
trget304.StillRunningAfter = ts
trget304.StillRunningAfter = server

cmd_tpl = "python tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/' | sed 's;ApacheTrafficServer\/[^ ]*;VERSION;'"
trget304.Processes.Default.Command = cmd_tpl.format(ts.Variables.port, 'data/{0}_get_304.txt'.format(HOST))
trget304.Processes.Default.TimeOut = 5  # seconds
trget304.Processes.Default.ReturnCode = 0
trget304.Processes.Default.Streams.stdout = "gold/http-get-304.gold"
