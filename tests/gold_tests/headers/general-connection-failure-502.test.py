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
ts = Test.MakeATSProcess("ts")

HOST = 'www.connectfail502.test'
ARBITRARY_LOOPBACK_IP='127.220.59.101' # This should fail to connect.

ts.Disk.remap_config.AddLine(
    'map http://{host} http://{ip}'.format(host=HOST, ip=ARBITRARY_LOOPBACK_IP)
)

Test.Setup.Copy(os.path.join(Test.Variables.AtsTestToolsDir, 'tcp_client.py'))

data_file=Test.Disk.File("www.connectfail502.test-get.txt", id="datafile")
data_file.WriteOn("GET / HTTP/1.1\r\nHost: {host}\r\n\r\n".format(host=HOST))

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = "python tcp_client.py 127.0.0.1 {0} {1} | egrep -v '^(Date: |Server: ATS/)'"\
        .format(ts.Variables.port, "www.connectfail502.test-get.txt")
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.Streams.stdout = 'general-connection-failure-502.gold'
