'''
Tests that HEAD requests return proper responses when origin fails
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
Tests that HEAD requests return proper responses when origin fails
'''

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

HOST = 'www.example.test'


Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
Test.Setup.Copy('data')

tr = Test.AddTestRun("Test domain {0}".format(HOST))
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.StillRunningAfter = ts

tr.Processes.Default.Command = "python3 tcp_client.py 127.0.0.1 {0} {1} | grep -v '^Date: '| grep -v '^Server: ATS/'".\
    format(ts.Variables.port, 'data/{0}_head.txt'.format(HOST))
tr.Processes.Default.TimeOut = 5  # seconds
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = "gold/http-head-no-origin.gold"
