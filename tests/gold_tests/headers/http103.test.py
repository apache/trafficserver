'''
Test the 103 Early Hints Response
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
import subprocess

Test.Summary = '''
Test the 103 Early Hints Response
'''

# need Curl
Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

Test.ContinueOnFail = True
HTTP_103_HOST = 'www.103earlyhints.test'

# Origin Server
server = Test.MakeOriginServer("server")
request_header = {"headers": "GET / HTTP/1.1\r\nHost: {0}\r\n\r\n".format(HTTP_103_HOST), "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload; as=style\r\n\r\nHTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

# ATS
ts = Test.MakeATSProcess("ts")
ts.Disk.remap_config.AddLine(
    'map http://{0}/ http://127.0.0.1:{1}/'.format(HTTP_103_HOST, server.Variables.Port)
)

# Test
tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'curl --proxy 127.0.0.1:{0} "http://{1}/" --verbose'.format(
    ts.Variables.port, HTTP_103_HOST)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stderr = "http103.gold"
