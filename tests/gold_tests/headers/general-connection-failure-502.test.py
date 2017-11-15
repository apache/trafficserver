'''
Test response when connection to origin fails
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
Test response when connection to origin fails
'''

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")

HOST = 'www.connectfail502.test'

ARBITRARY_LOOPBACK_IP='127.220.59.101' # This should fail to connect.
ts.Disk.remap_config.AddLine(
    'map http://{0} http://{1}'.format(HOST, ARBITRARY_LOOPBACK_IP)
)

Test.Setup.Copy(os.path.join(Test.Variables['AtsTestToolsDir'], 'tcp_client.py'))

TEST_DATA_PATH=os.path.join(Test.TestDirectory, 'www.connectfail502.test-get.txt')
with open(TEST_DATA_PATH, 'w') as f:
    f.write("GET / HTTP/1.1\r\nHost: {}\r\n\r\n".format(HOST))
Test.Setup.Copy(TEST_DATA_PATH)

GOLD_FILE_PATH=os.path.join(Test.TestDirectory, 'general-connection-failure-502.gold')
Test.Setup.Copy(GOLD_FILE_PATH)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | egrep -v '^(Date: |Server: ATS/)'"\
        .format(ts.Variables.port, os.path.basename(TEST_DATA_PATH))
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = os.path.basename(GOLD_FILE_PATH)
