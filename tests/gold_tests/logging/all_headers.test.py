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
import subprocess

Test.Summary = '''
Test new "all headers" log fields
'''
# need Curl
Test.SkipUnless(
    Condition.HasProgram(
        "curl", "Curl need to be installed on system for this test to work"),
    # Condition.IsPlatform("linux"), Don't see the need for this.
)

# Define ATS.
#
ts = Test.MakeATSProcess("ts")

# Define MicroServer.
#
server = Test.MakeOriginServer("server")

request_header = {"headers": "GET / HTTP/1.1\r\nHost: does.not.matter\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-control: max-age=85000\r\n\r\n",
    "timestamp": "1469733493.993", "body": "xxx"}
server.addResponse("sessionlog.json", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 0,
    'proxy.config.diags.debug.tags': 'http|dns',
})

ts.Disk.remap_config.AddLine(
    'map http://127.0.0.1:{0} http://127.0.0.1:{1}'.format(ts.Variables.port, server.Variables.Port)
)

# Mix in a numeric log field.  Hopefull this will detect any binary alignment problems.
#
ts.Disk.logging_yaml.AddLines(
    '''
formats:
  - name: custom
    format: " %<cqah> %<pssc> %<psah> %<ssah> %<pqah> %<cssah> " 
logs:
  - filename: test_all_headers
    format: custom
'''.split("\n")
)

# Configure comparison of "sanitized" log file with gold file at end of test.
#
Test.Disk.File(os.path.join(ts.Variables.LOGDIR, 'test_all_headers.log.san'),
               exists=True, content='gold/test_all_headers.gold')

# Ask the OS if the port is ready for connect()
#
def CheckPort(Port):
    return lambda: 0 == subprocess.call('netstat --listen --tcp -n | grep -q :{}'.format(Port), shell=True)

def reallyLong():
    value = 'abcdefghijklmnop'
    value = value + value
    value = value + value
    value = value + value
    retval = ""
    for i in range(3):
        retval += ' -H "x-header{}: {}"'.format(i, value)
    return retval

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server, ready=CheckPort(server.Variables.Port))
tr.Processes.Default.StartBefore(Test.Processes.ts, ready=CheckPort(ts.Variables.port))
tr.Processes.Default.Command = (
'curl "http://127.0.0.1:{0}" --user-agent "007" --verbose '.format(ts.Variables.port) + reallyLong()
)
tr.Processes.Default.ReturnCode = 0

# Repeat same curl, will be answered from the ATS cache.
#
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
'curl "http://127.0.0.1:{0}" --user-agent "007" --verbose '.format(ts.Variables.port) + reallyLong()
)
tr.Processes.Default.ReturnCode = 0

# Delay to allow TS to flush report to disk, then "sanitize" generated log.
#
tr = Test.AddTestRun()
tr.DelayStart = 10
tr.Processes.Default.Command = 'python {0} {3} < {1} > {2}'.format(
    os.path.join(Test.TestDirectory, 'all_headers_sanitizer.py'),
    os.path.join(ts.Variables.LOGDIR, 'test_all_headers.log'),
    os.path.join(ts.Variables.LOGDIR, 'test_all_headers.log.san'),
    server.Variables.Port)
tr.Processes.Default.ReturnCode = 0
