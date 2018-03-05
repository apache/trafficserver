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

import os
Test.Summary = '''
Test TTFB (time to first byte) timeouts
'''

TIMEOUT = 3

Test.SkipUnless(
    Condition.HasProgram("curl", "Curl need to be installed on system for this test to work")
)

Test.ContinueOnFail = True

ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")

ts.Disk.records_config.update({
    'proxy.config.http.transaction_no_activity_timeout_out': TIMEOUT,
    'proxy.config.http.connect_attempts_max_retries': 1,
    'proxy.config.http.connect_attempts_rr_retries': 1,
    'proxy.config.http.connect_attempts_timeout': TIMEOUT
})

timeout_hdrs = {"headers": "GET /timeout HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
no_timeout_hdrs = {"headers": "GET /no_timeout HTTP/1.1\r\nHost: *\r\n\r\n", "timestamp": "5678", "body": ""}
timeout_resp = {"headers": "HTTP/1.1 204 No Content\r\n\r\n", "timestamp": "5678", "body": "", "action": "delay: 5"}
no_timeout_resp = {"headers": "HTTP/1.1 204 No Content\r\n\r\n", "timestamp": "5678", "body": "", "action": "delay: 1"}

server.addResponse("sessionfile.log", timeout_hdrs, timeout_resp)
server.addResponse("sessionfile.log", no_timeout_hdrs, no_timeout_resp)

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://127.0.0.1:{1}'.format(ts.Variables.port, server.Variables.Port)
)

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -i http://127.0.0.1:{0}/timeout'.format(ts.Variables.port)
tr.Processes.Default.StartBefore(ts)
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.Streams.stdout = "gold/ttfb_no_reply.gold"
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -i http://127.0.0.1:{0}/no_timeout'.format(ts.Variables.port)
tr.Processes.Default.Streams.stdout = "gold/ttfb_reply.gold"
tr.Processes.Default.ReturnCode = 0
