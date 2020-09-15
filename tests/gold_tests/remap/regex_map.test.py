'''
Verify correct behavior of regex_map in remap.config.
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

Test.Summary = '''
Verify correct behavior of regex_map in remap.config.
'''

Test.ContinueOnFail = True
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")
dns = Test.MakeDNServer("dns", default='127.0.0.1')

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: zero.one.two.three.com\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}
server.addResponse("sessionfile.log", request_header, response_header)

ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http.*|dns|conf_remap',
    'proxy.config.http.referer_filter': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL'
})

ts.Disk.remap_config.AddLine(
    r'regex_map '
    r'http://(.*)?one\.two\.three\.com/ '
    r'http://$1reactivate.four.five.six.com:{}/'.format(server.Variables.Port)
)
ts.Disk.remap_config.AddLine(
    r'regex_map '
    r'https://\b(?!(.*one|two|three|four|five|six)).+\b\.seven\.eight\.nine\.com/blah12345.html '
    r'https://www.example.com:{}/one/two/three/blah12345.html'.format(server.Variables.Port)
)

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl -H"Host: zero.one.two.three.com" http://127.0.0.1:{0}/ --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/remap-zero-200.gold"
tr.StillRunningAfter = server
