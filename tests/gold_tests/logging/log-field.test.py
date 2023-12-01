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
Test log fields.
'''

ts = Test.MakeATSProcess("ts", enable_cache=False)
server = Test.MakeOriginServer("server")

request_header = {'timestamp': 100, "headers": "GET /test-1 HTTP/1.1\r\nHost: test-1\r\n\r\n", "body": ""}
response_header = {
    'timestamp': 100,
    "headers":
        "HTTP/1.1 200 OK\r\nTest: 1\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n",
    "body": "Test 1"
}
server.addResponse("sessionlog.json", request_header, response_header)
server.addResponse(
    "sessionlog.json", {
        'timestamp': 101,
        "headers": "GET /test-2 HTTP/1.1\r\nHost: test-2\r\n\r\n",
        "body": ""
    }, {
        'timestamp': 101,
        "headers":
            "HTTP/1.1 200 OK\r\nTest: 2\r\nContent-Type: application/jason\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n",
        "body": "Test 2"
    })
server.addResponse(
    "sessionlog.json", {
        'timestamp': 102,
        "headers": "GET /test-3 HTTP/1.1\r\nHost: test-3\r\n\r\n",
        "body": ""
    }, {
        'timestamp': 102,
        "headers": "HTTP/1.1 200 OK\r\nTest: 3\r\nConnection: close\r\nContent-Type: application/json\r\n\r\n",
        "body": "Test 3"
    })

nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

ts.Disk.records_config.update(
    {
        'proxy.config.net.connections_throttle': 100,
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL'
    })
# setup some config file for this server
ts.Disk.remap_config.AddLine('map / http://localhost:{}/'.format(server.Variables.Port))

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: custom
      format: '%<{Content-Type}essh>'
  logs:
    - filename: field-test
      format: custom
'''.split("\n"))

# #########################################################################
# at the end of the different test run a custom log file should exist
# Because of this we expect the testruns to pass the real test is if the
# customlog file exists and passes the format check
Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'field-test.log'), exists=True, content='gold/field-test.gold')

# first test is a miss for default
tr = Test.AddTestRun()
# Wait for the micro server
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(nameserver)
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts)

tr.Processes.Default.Command = 'curl --verbose --header "Host: test-1" http://localhost:{0}/test-1'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --verbose --header "Host: test-2" http://localhost:{0}/test-2'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl --verbose --header "Host: test-3" http://localhost:{0}/test-3'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
test_run = Test.AddTestRun()
test_run.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' + os.path.join(ts.Variables.LOGDIR, 'field-test.log'))
test_run.Processes.Default.ReturnCode = 0
