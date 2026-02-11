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

ts = Test.MakeATSProcess("ts", enable_cache=True)
server = Test.MakeOriginServer("server")

request_header = {'timestamp': 100, "headers": "GET /test-1 HTTP/1.1\r\nHost: test-1\r\n\r\n", "body": ""}
response_header = {
    'timestamp': 100,
    "headers":
        "HTTP/1.1 200 OK\r\nTest: 1\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n",
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
        'proxy.config.diags.debug.enabled': 1,
        'proxy.config.diags.debug.tags': 'http|log',
        #        'proxy.config.net.connections_throttle': 100,
        'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
        'proxy.config.dns.resolv_conf': 'NULL',
    })
# setup some config file for this server
ts.Disk.remap_config.AddLine('map / http://localhost:{}/'.format(server.Variables.Port))

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: custom
      format: 'mstsms:%<mstsms>'
  logs:
    - filename: field-mstsms
      format: custom
'''.split("\n"))

# first test is a miss for default
tr = Test.AddTestRun()
# Wait for the micro server
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(nameserver)
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts)

tr.MakeCurlCommand('--verbose --header "Host: test-1" http://localhost:{0}/test-1'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.MakeCurlCommand('--verbose --header "Host: test-2" http://localhost:{0}/test-2'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.MakeCurlCommand('--verbose --header "Host: test-3" http://localhost:{0}/test-3'.format(ts.Variables.port), ts=ts)
tr.Processes.Default.ReturnCode = 0


# check comma count and ensure last character is a digit
def check_lines(path):
    with open(path, 'r') as file:
        for line_num, line in enumerate(file, 1):
            line = line.rstrip('\n')
            comma_count = line.count(',')
            if comma_count != 19:
                return False, "Check comma count", f"Expected 19 commas, got {comma_count}"
            if not line[-1].isdigit():
                return False, "Check last char", f"Expected last character to be a digit got '{line[-1]}'"
    return True, "", ""


logpath = os.path.join(ts.Variables.LOGDIR, 'field-mstsms.log')

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
tr = Test.AddTestRun()
tr.Processes.Default.Command = (os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' + logpath)
#tr.Processes.Default.ReturnCode = 0
tr.Streams.All.Content = Testers.Lambda(lambda info, tester: check_lines(logpath))
