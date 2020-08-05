
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

Test.Summary = '''
Test a basic ip_resolve override using an ipv6 server
'''

Test.ContinueOnFail = True
# Define default ATS
ts = Test.MakeATSProcess("ts")
server = Test.MakeOriginServer("server")
server_v6 = Test.MakeOriginServer("server_v6", None, None, '::1', 0)

dns = Test.MakeDNServer("dns")

Test.testName = ""
request_header = {"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n",
                  "timestamp": "1469733493.993", "body": ""}
# expected response from the origin server
response_header = {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n",
                   "timestamp": "1469733493.993", "body": ""}

# add response to the server dictionary
server.addResponse("sessionfile.log", request_header, response_header)
server_v6.addResponse("sessionfile.log", request_header, response_header)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'http.*|dns|conf_remap',
    'proxy.config.http.referer_filter': 1,
    'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port),
    'proxy.config.dns.resolv_conf': 'NULL',
    'proxy.config.hostdb.ip_resolve': 'ipv4'
})


ts.Disk.remap_config.AddLine(
    'map http://testDNS.com http://test.ipv4.only.com:{0}  @plugin=conf_remap.so @pparam=proxy.config.hostdb.ip_resolve=ipv6;ipv4;client'.format(
        server.Variables.Port))
ts.Disk.remap_config.AddLine(
    'map http://testDNS2.com http://test.ipv6.only.com:{0}  @plugin=conf_remap.so @pparam=proxy.config.hostdb.ip_resolve=ipv6;only'.format(
        server_v6.Variables.Port))


dns.addRecords(records={"test.ipv4.only.com.": ["127.0.0.1"]})
dns.addRecords(records={"test.ipv6.only.com": ["127.0.0.1", "::1"]})

tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl  --proxy 127.0.0.1:{0} "http://testDNS.com" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server)
tr.Processes.Default.StartBefore(dns)
tr.Processes.Default.StartBefore(Test.Processes.ts)
tr.Processes.Default.Streams.stderr = "gold/remap-DNS-200.gold"
tr.StillRunningAfter = server


tr = Test.AddTestRun()
tr.Processes.Default.Command = 'curl  --proxy 127.0.0.1:{0} "http://testDNS2.com" --verbose'.format(ts.Variables.port)
tr.Processes.Default.ReturnCode = 0
tr.Processes.Default.StartBefore(server_v6)
tr.Processes.Default.Streams.stderr = "gold/remap-DNS-ipv6-200.gold"
tr.StillRunningAfter = server_v6
