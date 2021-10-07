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
Test pqsi and pqsp log fields.
'''

ts = Test.MakeATSProcess("ts", enable_cache=False)
server = Test.MakeOriginServer("server")

request_header = {
    "headers":
        "GET /test HTTP/1.1\r\n"
        "Host: whatever\r\n"
        "\r\n",
    "body": "",
    'timestamp': "1469733493.993",
}
response_header = {
    "headers":
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "\r\n",
    "body": "body\n",
    'timestamp': "1469733493.993",
}
server.addResponse("sessionlog.json", request_header, response_header)

nameserver = Test.MakeDNServer("dns", default='127.0.0.1')

ts.Disk.records_config.update({
    'proxy.config.dns.nameservers': f"127.0.0.1:{nameserver.Variables.Port}",
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.http.cache.http': 1,
    'proxy.config.http.cache.required_headers': 0,
})
ts.Disk.remap_config.AddLine(
    'map / http://localhost:{}/'.format(server.Variables.Port)
)

ts.Disk.logging_yaml.AddLines(
    '''
logging:
  formats:
    - name: custom
      format: '%<pqsi> %<pqsp>'
  logs:
    - filename: field-test
      format: custom
'''.split("\n")
)

tr = Test.AddTestRun()
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(nameserver)
# Delay on readiness of our ssl ports
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Command = f'curl --verbose http://localhost:{ts.Variables.port}/test'
tr.Processes.Default.ReturnCode = 0

# Response for this duplicate request should come from cache.
tr = Test.AddTestRun()
tr.Processes.Default.Command = f'curl --verbose http://localhost:{ts.Variables.port}/test'
tr.Processes.Default.ReturnCode = 0

log_filespec = os.path.join(ts.Variables.LOGDIR, 'field-test.log')

# Wait for log file to appear, then wait one extra second to make sure TS is done writing it.
tr = Test.AddTestRun()
tr.Processes.Default.Command = (
    os.path.join(Test.Variables.AtsTestToolsDir, 'condwait') + ' 60 1 -f ' + log_filespec
)
tr.Processes.Default.ReturnCode = 0

tr = Test.AddTestRun()
tr.Processes.Default.Command = "sed '1s/^127.0.0.1 [1-6][0-9]*$$/abc/' < " + log_filespec
tr.Processes.Default.Streams.stdout = "gold/pqsi-pqsp.gold"
tr.Processes.Default.ReturnCode = 0
