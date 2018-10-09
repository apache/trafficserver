'''
Test the 408 reponse header.
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
Check 408 response header for protocol stack data.
'''

Test.ContinueOnFail = True

# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

HTTP_408_HOST = 'www.http408.test'

request_header = {"headers": "GET / HTTP/1.1\r\nHost: {}\r\n\r\n".format(HTTP_408_HOST), "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.remap_config.AddLine(
    'map http://{0} http://127.0.0.1:{1}'.format(HTTP_408_HOST, server.Variables.Port)
)

TIMEOUT=2
ts.Disk.records_config.update({
    'proxy.config.http.transaction_no_activity_timeout_in': TIMEOUT,
})

Test.Setup.Copy(os.path.join(os.pardir, os.pardir, 'tools', 'tcp_client.py'))
Test.Setup.Copy('data')

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = 'python tcp_client.py 127.0.0.1 {0} {1} --delay-after-send {2}'\
        .format(ts.Variables.port, 'data/{0}.txt'.format(HTTP_408_HOST), TIMEOUT + 2)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.TimeOut = 10
tr.Processes.Default.Streams.stdout = "http408.gold"
